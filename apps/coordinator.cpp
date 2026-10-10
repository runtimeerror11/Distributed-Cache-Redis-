// coordinator: tracks membership via heartbeats, decides failover, and publishes the
// cluster view (with a new epoch) whenever a node fails or joins.
// Usage: coordinator --port 7000
#include <iostream>

#include "distcache/cluster/membership.h"
#include "distcache/cluster/ownership.h"

using namespace distcache;

int main() {
    Membership membership(std::chrono::milliseconds(1000), std::chrono::milliseconds(3000));
    OwnershipTable ownership;

    membership.set_listener([&](const NodeId& node, NodeState state) {
        // TODO(stage 4): on Dead -> promote the replica, bump epoch, broadcast new ClusterView.
        // TODO(stage 5): on a new node joining -> bump epoch, trigger rebalancing.
        (void)node;
        (void)state;
    });

    std::cout << "coordinator: not implemented yet\n";
    // TODO(stage 4): accept heartbeats over TCP and call membership.tick() periodically.
    return 0;
}
