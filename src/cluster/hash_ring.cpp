#include "distcache/cluster/hash_ring.h"

#include "distcache/common/hash.h"

namespace distcache {

uint64_t HashRing::point_of(const std::string &id, int i) {
    return hash64(id + "#" + std::to_string(i));
}

void HashRing::add_node(const std::string &id) {
    if (!nodes_.insert(id).second) return;  // already a member
    for (int i = 0; i < vnodes_; i++) {
        ring_.emplace(point_of(id, i), id);  // on a (very unlikely) collision the first owner keeps it
    }
}

void HashRing::remove_node(const std::string &id) {
    if (nodes_.erase(id) == 0) return;
    for (int i = 0; i < vnodes_; i++) {
        auto it = ring_.find(point_of(id, i));
        if (it != ring_.end() && it->second == id) ring_.erase(it);
    }
}

const std::string *HashRing::owner(const std::string &key) const {
    if (ring_.empty()) return nullptr;
    auto it = ring_.lower_bound(hash64(key));  // first point clockwise
    if (it == ring_.end()) it = ring_.begin();  // wrap around
    return &it->second;
}

const std::string &ModuloRouter::owner(const std::string &key) const {
    return nodes_.at(hash64(key) % nodes_.size());
}

}  // namespace distcache
