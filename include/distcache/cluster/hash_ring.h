#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "distcache/common/types.h"

namespace distcache {

// Consistent hashing ring with virtual nodes.
// Each physical node is placed at `vnodes_per_node` points on a 64-bit ring.
// A key belongs to the first vnode clockwise from hash(key).
class HashRing {
public:
    explicit HashRing(int vnodes_per_node = 100);

    void add_node(const NodeId& node);
    void remove_node(const NodeId& node);

    // Primary owner of a key.
    std::optional<NodeId> owner(const std::string& key) const;

    // First `n` distinct physical nodes clockwise: [primary, replica1, ...].
    std::vector<NodeId> preference_list(const std::string& key, std::size_t n) const;

    std::size_t node_count() const;

    static std::uint64_t hash(const std::string& s);  // e.g. FNV-1a 64 or MurmurHash

private:
    int vnodes_per_node_;
    std::map<std::uint64_t, NodeId> ring_;  // ring position -> physical node
};

// Baseline used for the "keys moved" comparison: owner = hash(key) % N.
class ModuloRouter {
public:
    explicit ModuloRouter(std::vector<NodeId> nodes) : nodes_(std::move(nodes)) {}
    NodeId owner(const std::string& key) const;

private:
    std::vector<NodeId> nodes_;
};

}  // namespace distcache
