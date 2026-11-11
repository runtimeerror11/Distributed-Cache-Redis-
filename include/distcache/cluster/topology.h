#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"

namespace distcache {

// One shard = one primary that owns a set of ring ranges + one dedicated replica
// that mirrors exactly that primary. Only shards are placed on the ring.
struct ShardInfo {
    std::string id;
    std::string primary;  // "host:port"
    std::string replica;  // "host:port", empty if the shard has no replica
};

// The cluster map: which shards exist and where their primary and replica run,
// versioned by an ownership epoch. The coordinator owns it; nodes and clients
// keep a copy and build the same ring from it.
//
// Text format (also used for the config file):
//   epoch 1
//   vnodes 100
//   shard s1 127.0.0.1:7101 127.0.0.1:7102
//   shard s2 127.0.0.1:7201 127.0.0.1:7202
// Blank lines and lines starting with '#' are ignored.
struct Topology {
    uint64_t epoch = 1;
    int vnodes = 100;
    std::vector<ShardInfo> shards;

    const ShardInfo *find(const std::string &shard_id) const;
    HashRing build_ring() const;
    std::string serialize() const;

    static bool parse(const std::string &text, Topology *out, std::string *err);
    static bool load_file(const std::string &path, Topology *out, std::string *err);
};

}  // namespace distcache
