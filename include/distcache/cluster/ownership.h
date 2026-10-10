#pragma once
#include <mutex>
#include <vector>

#include "distcache/cluster/hash_ring.h"
#include "distcache/common/types.h"

namespace distcache {

// The cluster view agreed on by all nodes: ring membership + who is primary, tagged by an epoch.
// The coordinator bumps the epoch on every failover or join. Nodes reject requests that carry an
// older epoch, so a primary that comes back after a failure cannot accept stale writes (no split-brain).
struct ClusterView {
    Epoch epoch = 0;
    std::vector<NodeAddress> nodes;
};

class OwnershipTable {
public:
    // Accepts the view only if its epoch is newer. Returns true if applied.
    bool apply(const ClusterView& view);
    Epoch epoch() const;
    ClusterView view() const;
    bool is_stale(Epoch request_epoch) const { return request_epoch < epoch(); }

private:
    mutable std::mutex mu_;
    ClusterView view_;
};

}  // namespace distcache
