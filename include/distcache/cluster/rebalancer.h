#pragma once
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"
#include "distcache/store/kv_store.h"

namespace distcache {

// When a node joins, finds keys whose owner changed between the old and new ring
// and streams only those keys to the new owner.
class Rebalancer {
public:
    // Keys in `local_keys` that `self` no longer owns under `new_ring`.
    static std::vector<std::string> keys_to_transfer(const NodeId& self,
                                                     const std::vector<std::string>& local_keys,
                                                     const HashRing& new_ring);
};

}  // namespace distcache
