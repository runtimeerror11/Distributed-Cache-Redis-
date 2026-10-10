#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "distcache/common/types.h"

namespace distcache {

enum class NodeState { Alive, Suspect, Dead };

// Heartbeat-based failure detector.
// A node that misses heartbeats for `suspect_after` becomes Suspect, then Dead after `dead_after`.
class Membership {
public:
    using Callback = std::function<void(const NodeId&, NodeState)>;

    Membership(std::chrono::milliseconds suspect_after, std::chrono::milliseconds dead_after);

    void on_heartbeat(const NodeId& node);
    void tick();  // run periodically; fires callbacks on state changes
    NodeState state(const NodeId& node) const;
    void set_listener(Callback cb) { listener_ = std::move(cb); }

private:
    std::chrono::milliseconds suspect_after_, dead_after_;
    mutable std::mutex mu_;
    std::unordered_map<NodeId, TimePoint> last_seen_;
    std::unordered_map<NodeId, NodeState> states_;
    Callback listener_;
};

}  // namespace distcache
