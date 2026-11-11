#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"
#include "distcache/cluster/topology.h"
#include "distcache/server/event_server.h"
#include "distcache/store/store.h"

namespace distcache {

enum class Role { Primary, Replica };

struct NodeOptions {
    std::string shard_id;
    Role role = Role::Primary;
    size_t max_bytes = 64u << 20;
    uint64_t idle_timeout_ms = 300 * 1000;
};

// One cache process: either the primary or the replica of one shard.
//
// Primary: serves GET/SET/DEL/EXPIRE/... for keys that hash to its shard,
// answers MOVED for other keys, and streams every change (including its own
// expiry and eviction decisions) to its replica, asynchronously.
//
// Replica: applies the primary's stream in order, serves reads, rejects
// client writes. It never expires or evicts on its own, so it stays an exact,
// promotable copy of the primary.
class CacheNode : public EventServer {
public:
    CacheNode(const Topology &topo, const NodeOptions &opts, uint16_t port);

    // Validates the options against the map. Must be called before listen/run.
    bool init(std::string *err);

    // Replication link state, primary side.
    enum class LinkState { None, Disconnected, Connecting, Syncing, Online };

protected:
    void handle_request(Conn *conn, std::vector<std::string> &cmd, Buffer &out) override;
    void on_connected(Conn *conn) override;
    void on_response(Conn *conn, const Value &v) override;
    void on_closed(Conn *conn) override;
    uint64_t next_timer_ms() override;
    void process_timers() override;

private:
    // Commands
    bool owns_key(const std::string &key, Buffer &out);
    bool reject_if_replica(Buffer &out);
    void do_get(std::vector<std::string> &cmd, Buffer &out);
    void do_set(std::vector<std::string> &cmd, Buffer &out);
    void do_del(std::vector<std::string> &cmd, Buffer &out);
    void do_expire(std::vector<std::string> &cmd, Buffer &out, int64_t unit_ms);
    void do_ttl(std::vector<std::string> &cmd, Buffer &out, int64_t unit_ms);
    void do_persist(std::vector<std::string> &cmd, Buffer &out);
    void do_keys(Buffer &out);
    void do_info(Buffer &out);
    void do_repl(Conn *conn, std::vector<std::string> &cmd, Buffer &out);

    // Replication, primary side
    void repl_send(std::vector<std::string> cmd);
    void repl_connect();
    void repl_full_sync();
    void repl_drop_link(const char *why);

    Topology topo_;
    HashRing ring_;
    NodeOptions opts_;
    uint64_t epoch_;
    Store store_;

    // Primary side of the replication link
    std::string replica_addr_;
    Conn *repl_conn_ = nullptr;
    LinkState link_ = LinkState::None;
    uint64_t repl_seq_ = 0;       // last operation sent
    uint64_t repl_acked_ = 0;     // last operation the replica confirmed
    uint64_t sync_end_seq_ = 0;   // last operation of the current full sync
    uint64_t full_syncs_ = 0;
    uint64_t next_reconnect_ms_ = 0;
    uint64_t next_repl_ping_ms_ = 0;
    bool reported_unreachable_ = false;

    // Replica side
    uint64_t applied_seq_ = 0;       // last operation applied from the primary
    uint64_t primary_seq_seen_ = 0;  // primary's latest sequence number, from its pings
};

}  // namespace distcache
