#pragma once
#include <string>
#include <vector>

#include "distcache/cluster/topology.h"
#include "distcache/server/event_server.h"

namespace distcache {

// The control plane. It owns the cluster map (shards, their primary and
// replica, the ownership epoch) and hands it out with GETMAP. It is never on
// the data path: clients send GET/SET/... straight to the primaries.
//
// Current stage: the map is static (loaded from a file).
// Next stage: heartbeats from every node, failure detection, replica promotion
// with an epoch bump, and controlled key migration when a shard joins.
class Coordinator : public EventServer {
public:
    Coordinator(const Topology &topo, uint16_t port) : EventServer(port), topo_(topo) {}

protected:
    void handle_request(Conn *conn, std::vector<std::string> &cmd, Buffer &out) override;

private:
    Topology topo_;
};

}  // namespace distcache
