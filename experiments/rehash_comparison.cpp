// Experiment: fraction of keys that change owner when a node is added,
// modulo hashing vs consistent hashing with virtual nodes.
// Expected: modulo moves ~N/(N+1) of keys, consistent hashing ~1/(N+1).
#include <iostream>
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"

using namespace distcache;

int main() {
    const int num_keys = 100000;
    const int start_nodes = 4;
    std::cout << "nodes_before,nodes_after,vnodes,modulo_moved_pct,consistent_moved_pct\n";

    // TODO(stage 2): for N in [start_nodes..10], vnodes in {1, 10, 100, 200}:
    //   build ModuloRouter and HashRing with N nodes, record owners of num_keys keys,
    //   add node N+1, count keys whose owner changed, print a CSV row.
    //   Then plot with scripts/plot_results.py.
    (void)num_keys;
    (void)start_nodes;
    return 0;
}
