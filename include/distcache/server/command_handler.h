#pragma once
#include <string>
#include <vector>

#include "distcache/cluster/ownership.h"
#include "distcache/replication/replicator.h"
#include "distcache/store/kv_store.h"

namespace distcache {

// Executes one parsed command and returns the RESP-encoded reply.
// If this node does not own the key it replies with a redirect: "-MOVED <node> <host:port>".
class CommandHandler {
public:
    CommandHandler(NodeId self, KvStore& store, OwnershipTable& ownership, Replicator& replicator);

    std::string handle(const std::vector<std::string>& args);

private:
    NodeId self_;
    KvStore& store_;
    OwnershipTable& ownership_;
    Replicator& replicator_;
};

}  // namespace distcache
