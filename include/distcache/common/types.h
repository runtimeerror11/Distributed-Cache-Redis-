#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace distcache {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

using NodeId = std::string;    // e.g. "node-1"
using Epoch = std::uint64_t;   // ownership epoch, increases on every failover/rebalance

struct NodeAddress {
    NodeId id;
    std::string host;
    std::uint16_t port = 0;
};

}  // namespace distcache
