#include "distcache/replication/replicator.h"

namespace distcache {

Replicator::Replicator(AckPolicy policy, std::chrono::milliseconds timeout)
    : policy_(policy), timeout_(timeout) {}

bool Replicator::replicate(const NodeAddress& replica, const ReplicationOp& op) {
    // TODO(stage 3): send op to replica over TCP.
    //   Async       -> queue it for a background thread, return true immediately.
    //   WaitReplica -> send and wait for ACK up to timeout_; return false on timeout.
    (void)replica;
    (void)op;
    return policy_ == AckPolicy::Async;
}

}  // namespace distcache
