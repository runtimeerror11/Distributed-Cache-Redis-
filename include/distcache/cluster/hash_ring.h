#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace distcache {

// Consistent hashing ring with virtual nodes.
// Each member (a shard) is placed at `vnodes` points on a 64-bit ring; a key
// belongs to the first point clockwise from hash(key), wrapping around.
// Adding or removing a member only moves the keys in the arcs it gains or
// loses (about 1/(N+1) of keys), instead of almost all keys with hash % N.
// Many points per member keep the load even and spread a newcomer's share
// across all existing members.
class HashRing {
public:
    explicit HashRing(int vnodes = 100) : vnodes_(vnodes < 1 ? 1 : vnodes) {}

    void add_node(const std::string &id);
    void remove_node(const std::string &id);
    // Owner of a key, or nullptr if the ring is empty.
    const std::string *owner(const std::string &key) const;

    size_t node_count() const { return nodes_.size(); }
    size_t point_count() const { return ring_.size(); }
    int vnodes() const { return vnodes_; }
    const std::set<std::string> &nodes() const { return nodes_; }

    static uint64_t point_of(const std::string &id, int i);

private:
    int vnodes_;
    std::map<uint64_t, std::string> ring_;  // ring position -> member id
    std::set<std::string> nodes_;
};

// Baseline for the "keys moved" comparison: owner = hash(key) % N.
class ModuloRouter {
public:
    explicit ModuloRouter(std::vector<std::string> nodes) : nodes_(std::move(nodes)) {}
    const std::string &owner(const std::string &key) const;

private:
    std::vector<std::string> nodes_;
};

}  // namespace distcache
