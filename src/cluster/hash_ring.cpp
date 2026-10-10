#include "distcache/cluster/hash_ring.h"

namespace distcache {

HashRing::HashRing(int vnodes_per_node) : vnodes_per_node_(vnodes_per_node) {}

std::uint64_t HashRing::hash(const std::string& s) {
    // FNV-1a 64-bit. Simple and well distributed enough for this project.
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

void HashRing::add_node(const NodeId& node) {
    // TODO(stage 2): insert vnodes_per_node_ points: ring_[hash(node + "#" + i)] = node.
    (void)node;
}

void HashRing::remove_node(const NodeId& node) {
    // TODO(stage 2): erase all of this node's points.
    (void)node;
}

std::optional<NodeId> HashRing::owner(const std::string& key) const {
    // TODO(stage 2): ring_.lower_bound(hash(key)), wrapping to begin().
    (void)key;
    return std::nullopt;
}

std::vector<NodeId> HashRing::preference_list(const std::string& key, std::size_t n) const {
    // TODO(stage 3): walk clockwise, skipping vnodes of nodes already chosen.
    (void)key;
    (void)n;
    return {};
}

std::size_t HashRing::node_count() const {
    return vnodes_per_node_ > 0 ? ring_.size() / static_cast<std::size_t>(vnodes_per_node_) : 0;
}

NodeId ModuloRouter::owner(const std::string& key) const {
    return nodes_.at(HashRing::hash(key) % nodes_.size());
}

}  // namespace distcache
