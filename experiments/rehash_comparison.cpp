// Experiment: how many keys change owner when the cluster grows or shrinks,
// modulo hashing vs consistent hashing with virtual nodes, and how evenly the
// keys are spread for different numbers of virtual nodes.
//
// Usage: rehash_experiment [--keys 100000] [--from 2] [--to 10] [--vnodes 100]
//
// Expected: adding one node to N moves ~N/(N+1) of keys with modulo hashing
// and ~1/(N+1) with consistent hashing.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "distcache/cluster/hash_ring.h"

using namespace distcache;

static std::vector<std::string> make_nodes(int n) {
    std::vector<std::string> v;
    for (int i = 1; i <= n; i++) v.push_back("shard-" + std::to_string(i));
    return v;
}

int main(int argc, char **argv) {
    int num_keys = 100000, from = 2, to = 10, vnodes = 100;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string f = argv[i];
        int v = std::atoi(argv[i + 1]);
        if (f == "--keys") num_keys = v;
        else if (f == "--from") from = v;
        else if (f == "--to") to = v;
        else if (f == "--vnodes") vnodes = v;
    }
    if (num_keys <= 0 || from < 1 || to <= from || vnodes < 1) {
        std::fprintf(stderr, "usage: rehash_experiment [--keys N] [--from N] [--to N] [--vnodes N]\n");
        return 2;
    }
    std::vector<std::string> keys;
    keys.reserve(size_t(num_keys));
    for (int i = 0; i < num_keys; i++) keys.push_back("key:" + std::to_string(i));

    // ---- 1. Keys moved when adding one node --------------------------------
    std::printf("1) Keys that change owner when one node is added (%d keys, %d vnodes)\n\n",
                num_keys, vnodes);
    std::printf("%-12s %12s %14s %12s\n", "nodes", "modulo", "consistent", "ideal 1/(N+1)");
    for (int n = from; n < to; n++) {
        std::vector<std::string> before = make_nodes(n), after = make_nodes(n + 1);
        ModuloRouter mod_before(before), mod_after(after);
        HashRing ring(vnodes);
        for (const auto &s : before) ring.add_node(s);
        std::vector<std::string> owner_before;
        owner_before.reserve(keys.size());
        for (const auto &k : keys) owner_before.push_back(*ring.owner(k));
        ring.add_node(after.back());

        size_t mod_moved = 0, ring_moved = 0;
        for (size_t i = 0; i < keys.size(); i++) {
            if (mod_before.owner(keys[i]) != mod_after.owner(keys[i])) mod_moved++;
            if (owner_before[i] != *ring.owner(keys[i])) ring_moved++;
        }
        char label[32];
        std::snprintf(label, sizeof(label), "%d -> %d", n, n + 1);
        std::printf("%-12s %11.1f%% %13.1f%% %12.1f%%\n", label, 100.0 * mod_moved / keys.size(),
                    100.0 * ring_moved / keys.size(), 100.0 / (n + 1));
    }

    // ---- 2. Balance vs number of virtual nodes ------------------------------
    const int n_balance = 5;
    std::printf("\n2) Load balance with %d nodes (share of keys per node; ideal %.1f%%)\n\n",
                n_balance, 100.0 / n_balance);
    std::printf("%-8s %10s %10s %12s %16s\n", "vnodes", "min", "max", "std dev", "max/ideal");
    for (int vn : {1, 10, 50, 100, 200, 500}) {
        HashRing ring(vn);
        for (const auto &s : make_nodes(n_balance)) ring.add_node(s);
        std::map<std::string, size_t> count;
        for (const auto &k : keys) count[*ring.owner(k)]++;
        double ideal = double(keys.size()) / n_balance, mn = 1e18, mx = 0, var = 0;
        for (const auto &s : make_nodes(n_balance)) {
            double c = double(count[s]);
            mn = std::min(mn, c);
            mx = std::max(mx, c);
            var += (c - ideal) * (c - ideal);
        }
        double sd = std::sqrt(var / n_balance);
        std::printf("%-8d %9.1f%% %9.1f%% %11.1f%% %15.2fx\n", vn, 100 * mn / keys.size(),
                    100 * mx / keys.size(), 100 * sd / keys.size(), mx / ideal);
    }

    // ---- 3. Removing a node only moves that node's keys --------------------
    std::printf("\n3) Removing one of 5 nodes (%d vnodes)\n\n", vnodes);
    {
        HashRing ring(vnodes);
        for (const auto &s : make_nodes(5)) ring.add_node(s);
        std::vector<std::string> owner_before;
        for (const auto &k : keys) owner_before.push_back(*ring.owner(k));
        ring.remove_node("shard-3");
        size_t moved = 0, moved_other = 0;
        for (size_t i = 0; i < keys.size(); i++) {
            if (owner_before[i] != *ring.owner(keys[i])) {
                moved++;
                if (owner_before[i] != "shard-3") moved_other++;
            }
        }
        std::printf("keys moved: %.1f%%  (keys that were not on the removed node: %zu)\n",
                    100.0 * moved / keys.size(), moved_other);
    }
    return 0;
}
