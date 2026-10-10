#include "distcache/cluster/rebalancer.h"

namespace distcache {

std::vector<std::string> Rebalancer::keys_to_transfer(const NodeId& self,
                                                      const std::vector<std::string>& local_keys,
                                                      const HashRing& new_ring) {
    // TODO(stage 5): return keys whose new_ring.owner(key) != self.
    (void)self;
    (void)local_keys;
    (void)new_ring;
    return {};
}

}  // namespace distcache
