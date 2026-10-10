#include "distcache/cluster/membership.h"

namespace distcache {

Membership::Membership(std::chrono::milliseconds suspect_after,
                       std::chrono::milliseconds dead_after)
    : suspect_after_(suspect_after), dead_after_(dead_after) {}

void Membership::on_heartbeat(const NodeId& node) {
    std::lock_guard lock(mu_);
    last_seen_[node] = Clock::now();
    // TODO(stage 4): if node was Suspect/Dead, mark Alive and notify listener_.
}

void Membership::tick() {
    // TODO(stage 4): compare now - last_seen_ against thresholds, update states_,
    // and call listener_ on every transition (Alive -> Suspect -> Dead).
}

NodeState Membership::state(const NodeId& node) const {
    std::lock_guard lock(mu_);
    auto it = states_.find(node);
    return it == states_.end() ? NodeState::Dead : it->second;
}

}  // namespace distcache
