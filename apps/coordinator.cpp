// coordinator: owns the cluster map and serves it to nodes and clients.
// Usage: coordinator --config config/cluster.conf [--port 7000]
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "distcache/common/log.h"
#include "distcache/server/coordinator.h"

using namespace distcache;

static void usage() {
    std::cerr << "usage: coordinator --config <file> [--port <port>]\n";
}

int main(int argc, char **argv) {
    std::string config;
    long port = 7000;
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        std::string val = argv[++i];
        if (flag == "--config") {
            config = val;
        } else if (flag == "--port") {
            port = std::strtol(val.c_str(), nullptr, 10);
        } else {
            usage();
            return 2;
        }
    }
    if (config.empty() || port <= 0 || port > 65535) {
        usage();
        return 2;
    }

    Topology topo;
    std::string err;
    if (!Topology::load_file(config, &topo, &err)) {
        DC_LOG("coordinator: %s", err.c_str());
        return 1;
    }

    EventServer::install_signal_handlers();
    Coordinator coord(topo, uint16_t(port));
    if (!coord.listen_now(&err)) {
        DC_LOG("coordinator: %s", err.c_str());
        return 1;
    }
    DC_LOG("coordinator listening on port %ld (epoch %llu, %zu shards)", port,
           (unsigned long long)topo.epoch, topo.shards.size());
    coord.run();
    DC_LOG("coordinator stopped");
    return 0;
}
