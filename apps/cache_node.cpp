// cache_node: one shard of the distributed cache.
// Usage: cache_node --id node-1 --port 7001 [--max-memory-mb 64] [--ack async|replica]
#include <cstdlib>
#include <iostream>
#include <string>

#include "distcache/cluster/ownership.h"
#include "distcache/replication/replicator.h"
#include "distcache/server/command_handler.h"
#include "distcache/server/tcp_server.h"
#include "distcache/store/kv_store.h"

using namespace distcache;

int main(int argc, char** argv) {
    std::string id = "node-1";
    std::uint16_t port = 7001;
    std::size_t max_mb = 64;
    AckPolicy ack = AckPolicy::Async;

    for (int i = 1; i + 1 < argc; i += 2) {
        std::string flag = argv[i], val = argv[i + 1];
        if (flag == "--id") id = val;
        else if (flag == "--port") port = static_cast<std::uint16_t>(std::stoi(val));
        else if (flag == "--max-memory-mb") max_mb = std::stoul(val);
        else if (flag == "--ack") ack = (val == "replica") ? AckPolicy::WaitReplica : AckPolicy::Async;
    }

    KvStore store(max_mb * 1024 * 1024);
    OwnershipTable ownership;
    Replicator replicator(ack, std::chrono::milliseconds(200));
    CommandHandler handler(id, store, ownership, replicator);

    // TODO(stage 1): start periodic expiry thread calling store.sweep_expired(20) every 100 ms.
    // TODO(stage 4): start heartbeat thread sending to the coordinator.

    std::cout << "Starting " << id << " on port " << port << "\n";
    TcpServer server(port, [&](const auto& args) { return handler.handle(args); });
    server.run();
    return EXIT_SUCCESS;
}
