// Experiment: hit rate and availability while nodes fail.
// Plan:
//   1. Start a cluster (scripts/run_cluster.sh), load K keys.
//   2. Run a client loop issuing GETs at a fixed rate, recording per-second:
//        availability = successful replies / requests, hit rate = hits / successful GETs.
//   3. Kill a primary mid-run (scripts/kill_node.sh), keep measuring through failover.
//   4. Print CSV: second,requests,ok,hits,availability,hit_rate
#include <iostream>

int main() {
    std::cout << "second,requests,ok,hits,availability,hit_rate\n";
    // TODO(stage 6)
    return 0;
}
