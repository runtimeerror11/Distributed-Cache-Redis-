#include "distcache/cluster/ownership.h"

namespace distcache {

bool OwnershipTable::apply(const ClusterView& view) {
    std::lock_guard lock(mu_);
    if (view.epoch <= view_.epoch) return false;  // ignore stale or duplicate views
    view_ = view;
    return true;
}

Epoch OwnershipTable::epoch() const {
    std::lock_guard lock(mu_);
    return view_.epoch;
}

ClusterView OwnershipTable::view() const {
    std::lock_guard lock(mu_);
    return view_;
}

}  // namespace distcache
