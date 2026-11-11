// cache_node: the primary or the replica of one shard.
//
// Usage:
//   cache_node --shard s1 --role primary|replica (--coord host:port | --config file)
//              [--maxmemory-mb 64] [--idle-timeout-ms 300000]
//
// The node loads the cluster map (from the coordinator, or from a file), finds
// its own shard and role in it, and listens on the port listed there.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "distcache/client/client.h"
#include "distcache/common/log.h"
#include "distcache/common/net.h"
#include "distcache/server/cache_node.h"

using namespace distcache;

static void usage() {
    std::cerr << "usage: cache_node --shard <id> --role primary|replica "
                 "(--coord <host:port> | --config <file>)\n"
                 "                  [--maxmemory-mb <n>] [--idle-timeout-ms <n>]\n";
}

int main(int argc, char **argv) {
    NodeOptions opts;
    std::string coord, config, role;
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        std::string val = argv[++i];
        if (flag == "--shard") {
            opts.shard_id = val;
        } else if (flag == "--role") {
            role = val;
        } else if (flag == "--coord") {
            coord = val;
        } else if (flag == "--config") {
            config = val;
        } else if (flag == "--maxmemory-mb") {
            opts.max_bytes = size_t(std::strtoull(val.c_str(), nullptr, 10)) << 20;
        } else if (flag == "--idle-timeout-ms") {
            opts.idle_timeout_ms = std::strtoull(val.c_str(), nullptr, 10);
        } else {
            usage();
            return 2;
        }
    }
    if (opts.shard_id.empty() || (role != "primary" && role != "replica") ||
        coord.empty() == config.empty() || opts.max_bytes == 0) {
        usage();
        return 2;
    }
    opts.role = (role == "primary") ? Role::Primary : Role::Replica;

    // Load the cluster map. From the coordinator, retry for a while in case it
    // is still starting up.
    Topology topo;
    std::string err;
    bool ok = false;
    if (!config.empty()) {
        ok = Topology::load_file(config, &topo, &err);
    } else {
        for (int attempt = 0; attempt < 50 && !ok; attempt++) {
            ok = fetch_topology(coord, &topo, &err);
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    if (!ok) {
        DC_LOG("cache_node: cannot load cluster map: %s", err.c_str());
        return 1;
    }

    const ShardInfo *shard = topo.find(opts.shard_id);
    std::string addr = !shard ? "" : (opts.role == Role::Primary ? shard->primary : shard->replica);
    std::string host;
    uint16_t port = 0;
    if (!shard || !parse_addr(addr, &host, &port)) {
        DC_LOG("cache_node: no %s address for shard %s in the map", role.c_str(),
               opts.shard_id.c_str());
        return 1;
    }

    EventServer::install_signal_handlers();
    CacheNode node(topo, opts, port);
    if (!node.init(&err) || !node.listen_now(&err)) {
        DC_LOG("cache_node: %s", err.c_str());
        return 1;
    }
    DC_LOG("%s of shard %s listening on port %u (epoch %llu, maxmemory %zu MiB)", role.c_str(),
           opts.shard_id.c_str(), unsigned(port), (unsigned long long)topo.epoch,
           opts.max_bytes >> 20);
    node.run();
    DC_LOG("%s of shard %s stopped", role.c_str(), opts.shard_id.c_str());
    return 0;
}
