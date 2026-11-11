#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"
#include "distcache/cluster/topology.h"
#include "distcache/protocol/protocol.h"

namespace distcache {

// One persistent, blocking connection to a node, with connect and I/O timeouts
// so a dead peer can never hang the caller.
class Connection {
public:
    explicit Connection(int io_timeout_ms = 1000) : io_timeout_ms_(io_timeout_ms) {}
    ~Connection() { close(); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    bool connect(const std::string &addr, std::string *err);
    bool connected() const { return fd_ >= 0; }
    void close();
    // Sends one request and waits for its response. On a transport failure the
    // connection is closed and false is returned.
    bool call(const std::vector<std::string> &cmd, Value *out, std::string *err);

private:
    int fd_ = -1;
    int io_timeout_ms_;
};

// Fetches the cluster map from the coordinator.
bool fetch_topology(const std::string &coord_addr, Topology *out, std::string *err,
                    int io_timeout_ms = 1000);

// True for commands whose second argument is a key, i.e. that can be routed.
bool is_key_command(const std::string &cmd_name);

// Client-side routing: hash the key on the same ring as the nodes, send the
// command straight to that shard's primary over a cached connection. The
// coordinator is only contacted to (re)load the map: at start, when a node
// answers MOVED, or when a node cannot be reached.
class ClusterClient {
public:
    explicit ClusterClient(std::string coord_addr, int io_timeout_ms = 1000)
        : coord_(std::move(coord_addr)), io_timeout_ms_(io_timeout_ms) {}

    bool refresh(std::string *err);
    // Routes by cmd[1]. Retries up to `max_attempts` times on MOVED or transport errors.
    Value call(const std::vector<std::string> &cmd, int max_attempts = 3);
    // Sends to one node directly, without routing.
    Value call_node(const std::string &addr, const std::vector<std::string> &cmd);

    const Topology &topology() const { return topo_; }
    // Shard id and primary address for a key ("" if the map is not loaded).
    std::string shard_for(const std::string &key) const;
    std::string primary_for(const std::string &key) const;

    uint64_t refreshes() const { return refreshes_; }

private:
    Connection *conn_for(const std::string &addr, std::string *err);

    std::string coord_;
    int io_timeout_ms_;
    Topology topo_;
    HashRing ring_;
    bool have_map_ = false;
    uint64_t refreshes_ = 0;
    std::map<std::string, std::unique_ptr<Connection>> conns_;
};

}  // namespace distcache
