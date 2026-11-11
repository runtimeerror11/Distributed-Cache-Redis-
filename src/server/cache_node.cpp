#include "distcache/server/cache_node.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>

#include "distcache/common/log.h"
#include "distcache/common/net.h"
#include "distcache/common/time.h"

namespace distcache {

static const size_t k_max_expire_work = 2000;            // keys expired per loop iteration
static const uint64_t k_reconnect_interval_ms = 1000;    // retry the replica link every 1 s
static const uint64_t k_repl_ping_interval_ms = 1000;    // keep-alive + progress report
static const size_t k_repl_buffer_max = 256u << 20;      // unsent replication data cap

static std::string lower(const std::string &s) {
    std::string r = s;
    for (char &ch : r) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return r;
}

static bool str2int(const std::string &s, int64_t &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || *end != '\0') return false;
    out = v;
    return true;
}

static bool str2u64(const std::string &s, uint64_t &out) {
    if (s.empty() || s[0] == '-') return false;
    char *end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (errno != 0 || *end != '\0') return false;
    out = v;
    return true;
}

static const char *link_name(CacheNode::LinkState s) {
    switch (s) {
        case CacheNode::LinkState::None: return "none";
        case CacheNode::LinkState::Disconnected: return "disconnected";
        case CacheNode::LinkState::Connecting: return "connecting";
        case CacheNode::LinkState::Syncing: return "syncing";
        case CacheNode::LinkState::Online: return "online";
    }
    return "?";
}

CacheNode::CacheNode(const Topology &topo, const NodeOptions &opts, uint16_t port)
    : EventServer(port),
      topo_(topo),
      ring_(topo.build_ring()),
      opts_(opts),
      epoch_(topo.epoch),
      store_(opts.max_bytes) {
    // The primary's own deletions (expiry, eviction) are decisions the replica
    // must copy, so they are replicated like client writes.
    store_.set_delete_hook([this](const std::string &key, DeleteReason) {
        repl_send({"repl.del", key});
    });
}

bool CacheNode::init(std::string *err) {
    const ShardInfo *shard = topo_.find(opts_.shard_id);
    if (!shard) {
        if (err) *err = "shard " + opts_.shard_id + " is not in the cluster map";
        return false;
    }
    if (opts_.role == Role::Replica) {
        if (shard->replica.empty()) {
            if (err) *err = "shard " + opts_.shard_id + " has no replica in the map";
            return false;
        }
        store_.set_authoritative(false);
    } else if (!shard->replica.empty()) {
        replica_addr_ = shard->replica;
        link_ = LinkState::Disconnected;
        next_reconnect_ms_ = 0;  // connect on the first loop iteration
    }
    set_idle_timeout_ms(opts_.idle_timeout_ms);
    return true;
}

// ---- ownership --------------------------------------------------------------

// A key is served only by the shard it hashes to. Anything else gets MOVED with
// the owner's address, so a client with a stale map knows to refresh it.
bool CacheNode::owns_key(const std::string &key, Buffer &out) {
    const std::string *shard = ring_.owner(key);
    if (shard && *shard == opts_.shard_id) return true;
    const ShardInfo *info = shard ? topo_.find(*shard) : nullptr;
    out_err(out, ERR_MOVED,
            "MOVED " + (shard ? *shard : std::string("?")) + " " + (info ? info->primary : ""));
    return false;
}

bool CacheNode::reject_if_replica(Buffer &out) {
    if (opts_.role != Role::Replica) return false;
    const ShardInfo *info = topo_.find(opts_.shard_id);
    out_err(out, ERR_READONLY,
            "READONLY replica of shard " + opts_.shard_id + "; write to " + info->primary);
    return true;
}

// ---- commands ---------------------------------------------------------------

void CacheNode::handle_request(Conn *conn, std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.empty()) return out_err(out, ERR_BAD_ARG, "empty command");
    std::string name = lower(cmd[0]);
    size_t n = cmd.size();
    auto arity = [&](size_t want) {
        if (n == want) return true;
        out_err(out, ERR_BAD_ARG, "wrong number of arguments for '" + name + "'");
        return false;
    };

    if (name.compare(0, 5, "repl.") == 0) return do_repl(conn, cmd, out);
    if (name == "ping") return out_str(out, "PONG");
    if (name == "get") return arity(2) ? do_get(cmd, out) : void();
    if (name == "set") return arity(3) ? do_set(cmd, out) : void();
    if (name == "del") return arity(2) ? do_del(cmd, out) : void();
    if (name == "expire") return arity(3) ? do_expire(cmd, out, 1000) : void();
    if (name == "pexpire") return arity(3) ? do_expire(cmd, out, 1) : void();
    if (name == "ttl") return arity(2) ? do_ttl(cmd, out, 1000) : void();
    if (name == "pttl") return arity(2) ? do_ttl(cmd, out, 1) : void();
    if (name == "persist") return arity(2) ? do_persist(cmd, out) : void();
    if (name == "keys") return arity(1) ? do_keys(out) : void();
    if (name == "dbsize") return arity(1) ? out_int(out, int64_t(store_.size())) : void();
    if (name == "info") return arity(1) ? do_info(out) : void();
    out_err(out, ERR_UNKNOWN, "unknown command '" + cmd[0] + "'");
}

void CacheNode::do_get(std::vector<std::string> &cmd, Buffer &out) {
    if (!owns_key(cmd[1], out)) return;
    const std::string *val = store_.get(cmd[1]);
    if (val) {
        out_str(out, *val);
    } else {
        out_nil(out);
    }
}

void CacheNode::do_set(std::vector<std::string> &cmd, Buffer &out) {
    if (!owns_key(cmd[1], out) || reject_if_replica(out)) return;
    store_.set(cmd[1], cmd[2]);
    repl_send({"repl.set", cmd[1], cmd[2], "0"});
    out_str(out, "OK");
}

void CacheNode::do_del(std::vector<std::string> &cmd, Buffer &out) {
    if (!owns_key(cmd[1], out) || reject_if_replica(out)) return;
    bool existed = store_.del(cmd[1]);
    if (existed) repl_send({"repl.del", cmd[1]});
    out_int(out, existed ? 1 : 0);
}

// EXPIRE key seconds / PEXPIRE key ms. A TTL <= 0 deletes the key.
// The replica receives the absolute deadline, not the relative TTL, so the
// replication delay does not extend the key's lifetime.
void CacheNode::do_expire(std::vector<std::string> &cmd, Buffer &out, int64_t unit_ms) {
    int64_t ttl = 0;
    if (!str2int(cmd[2], ttl) || ttl > INT64_MAX / unit_ms || ttl < INT64_MIN / unit_ms) {
        return out_err(out, ERR_BAD_ARG, "value is not an integer or out of range");
    }
    if (!owns_key(cmd[1], out) || reject_if_replica(out)) return;
    bool existed = store_.expire(cmd[1], ttl * unit_ms);
    if (existed) {
        if (ttl <= 0) {
            repl_send({"repl.del", cmd[1]});
        } else {
            repl_send({"repl.expireat", cmd[1], std::to_string(store_.deadline(cmd[1]))});
        }
    }
    out_int(out, existed ? 1 : 0);
}

void CacheNode::do_ttl(std::vector<std::string> &cmd, Buffer &out, int64_t unit_ms) {
    if (!owns_key(cmd[1], out)) return;
    int64_t ms = store_.pttl(cmd[1]);
    if (ms < 0) return out_int(out, ms);  // -2 no key, -1 no TTL
    out_int(out, unit_ms == 1 ? ms : (ms + unit_ms / 2) / unit_ms);
}

void CacheNode::do_persist(std::vector<std::string> &cmd, Buffer &out) {
    if (!owns_key(cmd[1], out) || reject_if_replica(out)) return;
    bool changed = store_.persist(cmd[1]);
    if (changed) repl_send({"repl.expireat", cmd[1], "0"});
    out_int(out, changed ? 1 : 0);
}

void CacheNode::do_keys(Buffer &out) {
    std::vector<std::string> keys;
    store_.foreach([&](const Entry &e) { keys.push_back(e.key); });
    out_arr(out, uint32_t(keys.size()));
    for (const std::string &k : keys) out_str(out, k);
}

void CacheNode::do_info(Buffer &out) {
    const StoreStats &st = store_.stats();
    std::string s;
    auto line = [&](const std::string &k, const std::string &v) { s += k + ":" + v + "\n"; };
    line("shard", opts_.shard_id);
    line("role", opts_.role == Role::Primary ? "primary" : "replica");
    line("epoch", std::to_string(epoch_));
    line("connected_clients", std::to_string(client_count()));
    line("keys", std::to_string(store_.size()));
    line("used_bytes", std::to_string(store_.used_bytes()));
    line("max_bytes", std::to_string(store_.max_bytes()));
    line("hits", std::to_string(st.hits));
    line("misses", std::to_string(st.misses));
    line("evictions", std::to_string(st.evictions));
    line("expired", std::to_string(st.expired));
    if (opts_.role == Role::Primary) {
        line("replica", replica_addr_.empty() ? "none" : replica_addr_);
        line("repl_link", link_name(link_));
        line("repl_seq", std::to_string(repl_seq_));
        line("repl_acked", std::to_string(repl_acked_));
        line("repl_lag_ops", std::to_string(repl_seq_ - std::min(repl_seq_, repl_acked_)));
        line("full_syncs", std::to_string(full_syncs_));
    } else {
        line("applied_seq", std::to_string(applied_seq_));
        line("primary_seq_seen", std::to_string(primary_seq_seen_));
    }
    out_str(out, s);
}

// ---- replication, replica side ----------------------------------------------
//
// Messages: repl.<op> <epoch> <seq> [args...]
//   repl.reset                          start of a full sync: drop everything
//   repl.set      <key> <val> <deadline>
//   repl.del      <key>
//   repl.expireat <key> <deadline>      deadline 0 removes the TTL
//   repl.ping                           keep-alive; seq = primary's latest seq
// Every message is answered with the last applied sequence number (the ack).
void CacheNode::do_repl(Conn *conn, std::vector<std::string> &cmd, Buffer &out) {
    std::string name = lower(cmd[0]);
    if (opts_.role != Role::Replica) return out_err(out, ERR_BAD_ARG, "not a replica");
    uint64_t epoch = 0, seq = 0;
    if (cmd.size() < 3 || !str2u64(cmd[1], epoch) || !str2u64(cmd[2], seq)) {
        return out_err(out, ERR_BAD_ARG, "bad replication header");
    }
    // Ownership fencing: never apply changes from an older ownership epoch.
    if (epoch < epoch_) {
        return out_err(out, ERR_STALE_EPOCH, "stale epoch " + std::to_string(epoch));
    }
    set_idle_exempt(conn);  // a quiet replication link is normal

    if (name == "repl.ping") {
        primary_seq_seen_ = std::max(primary_seq_seen_, seq);
        return out_int(out, int64_t(applied_seq_));
    }
    if (name == "repl.reset") {
        store_.clear();
        applied_seq_ = seq;
        primary_seq_seen_ = seq;
        DC_LOG("full sync from primary started");
        return out_int(out, int64_t(applied_seq_));
    }
    // TCP keeps the stream in order; a gap means something is badly wrong, so
    // refuse it and let the primary drop the link and resync from scratch.
    if (seq != applied_seq_ + 1) {
        return out_err(out, ERR_BAD_ARG,
                       "out of order: got " + std::to_string(seq) + ", expected " +
                           std::to_string(applied_seq_ + 1));
    }
    uint64_t deadline = 0;
    if (name == "repl.set" && cmd.size() == 6 && str2u64(cmd[5], deadline)) {
        store_.apply_set(cmd[3], cmd[4], deadline);
    } else if (name == "repl.del" && cmd.size() == 4) {
        store_.apply_del(cmd[3]);
    } else if (name == "repl.expireat" && cmd.size() == 5 && str2u64(cmd[4], deadline)) {
        store_.apply_expire_at(cmd[3], deadline);
    } else {
        return out_err(out, ERR_BAD_ARG, "bad replication message '" + cmd[0] + "'");
    }
    applied_seq_ = seq;
    primary_seq_seen_ = std::max(primary_seq_seen_, seq);
    out_int(out, int64_t(applied_seq_));
}

// ---- replication, primary side ----------------------------------------------
//
// Asynchronous: the client already got its reply; the change is queued in the
// replication connection's output buffer and the event loop sends it. Nothing
// blocks. While the link is down, changes are not queued: the next connection
// starts with a full sync that replaces the replica's contents anyway.
void CacheNode::repl_send(std::vector<std::string> cmd) {
    if (link_ != LinkState::Syncing && link_ != LinkState::Online) return;
    if (!repl_conn_ || repl_conn_->want_close) return;
    cmd.insert(cmd.begin() + 1, {std::to_string(epoch_), std::to_string(++repl_seq_)});
    send_request(repl_conn_, cmd);
    if (repl_conn_->outgoing.size() > k_repl_buffer_max) {
        repl_drop_link("replication buffer full (replica too slow)");
    }
}

void CacheNode::repl_connect() {
    std::string host, err;
    uint16_t port = 0;
    parse_addr(replica_addr_, &host, &port);
    Conn *c = connect_peer(host, port, &err);
    if (!c) {
        next_reconnect_ms_ = mono_ms() + k_reconnect_interval_ms;
        return;
    }
    repl_conn_ = c;
    link_ = LinkState::Connecting;
}

// Snapshot + stream. The snapshot is queued in one loop iteration, so every
// later write lands after it in the same ordered TCP stream: the replica ends
// up with exactly the primary's data without pausing writes.
void CacheNode::repl_full_sync() {
    full_syncs_++;
    repl_send({"repl.reset"});
    size_t count = 0;
    store_.foreach([&](const Entry &e) {
        repl_send({"repl.set", e.key, e.val, std::to_string(store_.deadline_of(e))});
        count++;
    });
    sync_end_seq_ = repl_seq_;
    DC_LOG("full sync to replica %s: %zu keys queued", replica_addr_.c_str(), count);
}

void CacheNode::repl_drop_link(const char *why) {
    if (!repl_conn_) return;
    DC_LOG("dropping replication link: %s", why);
    repl_conn_->want_close = true;
}

void CacheNode::on_connected(Conn *conn) {
    if (conn != repl_conn_) return;
    link_ = LinkState::Syncing;
    reported_unreachable_ = false;
    DC_LOG("connected to replica %s", replica_addr_.c_str());
    repl_full_sync();
    next_repl_ping_ms_ = mono_ms() + k_repl_ping_interval_ms;
}

void CacheNode::on_response(Conn *conn, const Value &v) {
    if (conn != repl_conn_) return;
    if (v.is_err()) {
        DC_LOG("replica rejected a message: %s", v.str.c_str());
        repl_drop_link("replica error");
        return;
    }
    if (v.tag == TAG_INT && uint64_t(v.i) > repl_acked_) repl_acked_ = uint64_t(v.i);
    if (link_ == LinkState::Syncing && repl_acked_ >= sync_end_seq_) {
        link_ = LinkState::Online;
        DC_LOG("replica %s is in sync (seq %llu)", replica_addr_.c_str(),
               (unsigned long long)repl_acked_);
    }
}

void CacheNode::on_closed(Conn *conn) {
    if (conn != repl_conn_) return;
    LinkState was = link_;
    repl_conn_ = nullptr;
    link_ = LinkState::Disconnected;
    next_reconnect_ms_ = mono_ms() + k_reconnect_interval_ms;
    if (was == LinkState::Syncing || was == LinkState::Online) {
        DC_LOG("replication link to %s lost; reconnecting", replica_addr_.c_str());
    } else if (!reported_unreachable_) {
        DC_LOG("replica %s unreachable; retrying every %llu ms", replica_addr_.c_str(),
               (unsigned long long)k_reconnect_interval_ms);
        reported_unreachable_ = true;
    }
}

// ---- timers -----------------------------------------------------------------

uint64_t CacheNode::next_timer_ms() {
    uint64_t now = mono_ms();
    uint64_t next = UINT64_MAX;
    // Key expiry: the heap's nearest deadline (wall clock) converted to the
    // monotonic clock the loop uses. Replicas skip it: they never expire keys
    // themselves, so waking up for them would only spin.
    if (store_.authoritative()) {
        uint64_t deadline = store_.next_deadline();
        if (deadline) {
            uint64_t wall = wall_ms();
            next = deadline <= wall ? now : now + (deadline - wall);
        }
    }
    if (link_ == LinkState::Disconnected) next = std::min(next, next_reconnect_ms_);
    if (link_ == LinkState::Syncing || link_ == LinkState::Online) {
        next = std::min(next, next_repl_ping_ms_);
    }
    return next;
}

void CacheNode::process_timers() {
    // Active expiry, capped per iteration so a mass expiry never stalls the
    // loop. If more keys are due, the next poll() timeout is 0 and it continues.
    store_.process_expired(k_max_expire_work);

    uint64_t now = mono_ms();
    if (link_ == LinkState::Disconnected && now >= next_reconnect_ms_) repl_connect();
    if ((link_ == LinkState::Syncing || link_ == LinkState::Online) && now >= next_repl_ping_ms_ &&
        repl_conn_ && !repl_conn_->want_close) {
        send_request(repl_conn_, {"repl.ping", std::to_string(epoch_), std::to_string(repl_seq_)});
        next_repl_ping_ms_ = now + k_repl_ping_interval_ms;
    }
}

}  // namespace distcache
