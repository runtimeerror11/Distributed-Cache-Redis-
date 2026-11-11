#include "distcache/client/client.h"

#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#include "distcache/common/net.h"

namespace distcache {

bool Connection::connect(const std::string &addr, std::string *err) {
    close();
    std::string host;
    uint16_t port = 0;
    if (!parse_addr(addr, &host, &port)) {
        if (err) *err = "bad address " + addr;
        return false;
    }
    fd_ = connect_blocking(host, port, io_timeout_ms_, io_timeout_ms_, err);
    return fd_ >= 0;
}

void Connection::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool Connection::call(const std::vector<std::string> &cmd, Value *out, std::string *err) {
    if (fd_ < 0) {
        if (err) *err = "not connected";
        return false;
    }
    Buffer req;
    encode_request(req, cmd);
    if (req.size() > 4 + k_max_msg) {
        if (err) *err = "request too large";
        return false;
    }
    if (!write_all(fd_, req.data(), req.size())) {
        if (err) *err = "write failed";
        close();
        return false;
    }
    uint32_t len = 0;
    if (!read_full(fd_, &len, 4) || len > k_max_msg) {
        if (err) *err = "read failed or timed out";
        close();
        return false;
    }
    std::vector<uint8_t> body(len);
    if (len > 0 && !read_full(fd_, body.data(), len)) {
        if (err) *err = "read failed or timed out";
        close();
        return false;
    }
    if (!parse_response(body.data(), len, out)) {
        if (err) *err = "malformed response";
        close();
        return false;
    }
    return true;
}

bool fetch_topology(const std::string &coord_addr, Topology *out, std::string *err,
                    int io_timeout_ms) {
    Connection c(io_timeout_ms);
    if (!c.connect(coord_addr, err)) return false;
    Value v;
    if (!c.call({"getmap"}, &v, err)) return false;
    if (v.tag != TAG_STR) {
        if (err) *err = "coordinator replied: " + v.to_string();
        return false;
    }
    return Topology::parse(v.str, out, err);
}

bool is_key_command(const std::string &name) {
    std::string n;
    for (char ch : name) n += char(std::tolower(static_cast<unsigned char>(ch)));
    return n == "get" || n == "set" || n == "del" || n == "expire" || n == "pexpire" ||
           n == "ttl" || n == "pttl" || n == "persist";
}

bool ClusterClient::refresh(std::string *err) {
    Topology t;
    if (!fetch_topology(coord_, &t, err, io_timeout_ms_)) return false;
    topo_ = t;
    ring_ = topo_.build_ring();
    have_map_ = true;
    refreshes_++;
    return true;
}

std::string ClusterClient::shard_for(const std::string &key) const {
    const std::string *s = have_map_ ? ring_.owner(key) : nullptr;
    return s ? *s : std::string();
}

std::string ClusterClient::primary_for(const std::string &key) const {
    std::string shard = shard_for(key);
    const ShardInfo *info = shard.empty() ? nullptr : topo_.find(shard);
    return info ? info->primary : std::string();
}

Connection *ClusterClient::conn_for(const std::string &addr, std::string *err) {
    std::unique_ptr<Connection> &c = conns_[addr];
    if (!c) c.reset(new Connection(io_timeout_ms_));
    if (!c->connected() && !c->connect(addr, err)) return nullptr;
    return c.get();
}

Value ClusterClient::call_node(const std::string &addr, const std::vector<std::string> &cmd) {
    std::string err;
    Connection *c = conn_for(addr, &err);
    Value v;
    if (!c || !c->call(cmd, &v, &err)) return Value::error(ERR_UNAVAILABLE, addr + ": " + err);
    return v;
}

Value ClusterClient::call(const std::vector<std::string> &cmd, int max_attempts) {
    if (cmd.size() < 2 || !is_key_command(cmd[0])) {
        return Value::error(ERR_BAD_ARG, "command has no key to route by; send it to a node");
    }
    Value last = Value::error(ERR_UNAVAILABLE, "no attempt made");
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        std::string err;
        if (attempt > 0) {
            // Something changed (node down or ownership moved): back off a little
            // and reload the map before trying again.
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            have_map_ = false;
        }
        if (!have_map_ && !refresh(&err)) {
            last = Value::error(ERR_UNAVAILABLE, "coordinator " + coord_ + ": " + err);
            continue;
        }
        std::string addr = primary_for(cmd[1]);
        last = call_node(addr, cmd);
        bool retry = (last.is_err() && (last.code == ERR_MOVED || last.code == ERR_UNAVAILABLE));
        if (!retry) return last;
    }
    return last;
}

}  // namespace distcache
