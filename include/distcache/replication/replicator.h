#pragma once
#include <chrono>
#include <string>
#include <vector>

#include "distcache/common/types.h"

namespace distcache {

// Write acknowledgement policy (document the choice in docs/consistency.md).
enum class AckPolicy {
    Async,         // reply to the client after the primary applies the write; replicate in background
    WaitReplica,   // reply only after the replica also acknowledges (or timeout -> error)
};

struct ReplicationOp {
    Epoch epoch;
    std::uint64_t seq;          // per-primary sequence number, used for replica catch-up
    std::string command;        // SET / DEL / EXPIRE
    std::vector<std::string> args;
};

class Replicator {
public:
    Replicator(AckPolicy policy, std::chrono::milliseconds timeout);

    // Sends the op to the replica for this key. Returns true when the policy is satisfied.
    bool replicate(const NodeAddress& replica, const ReplicationOp& op);

    AckPolicy policy() const { return policy_; }

private:
    AckPolicy policy_;
    std::chrono::milliseconds timeout_;
    std::uint64_t next_seq_ = 0;
};

}  // namespace distcache
