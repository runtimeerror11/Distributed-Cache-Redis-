#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "distcache/cluster/hash_ring.h"

using namespace distcache;

namespace {
std::vector<std::string> keys(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; i++) v.push_back("key:" + std::to_string(i));
    return v;
}
}  // namespace

TEST(ring_every_key_has_a_stable_owner) {
    HashRing a(100), b(100);
    CHECK(a.owner("x") == nullptr);
    for (const char *s : {"s1", "s2", "s3"}) {
        a.add_node(s);
        b.add_node(s);
    }
    CHECK_EQ(a.point_count(), size_t(300));
    bool same = true;
    for (const auto &k : keys(5000)) {
        // Same members -> same owner on every machine (client and nodes agree).
        if (!a.owner(k) || *a.owner(k) != *b.owner(k)) same = false;
    }
    CHECK(same);
}

TEST(ring_adding_a_node_moves_few_keys) {
    auto ks = keys(20000);
    HashRing ring(100);
    std::vector<std::string> nodes = {"s1", "s2", "s3", "s4"};
    for (const auto &n : nodes) ring.add_node(n);
    std::vector<std::string> before;
    for (const auto &k : ks) before.push_back(*ring.owner(k));
    ring.add_node("s5");
    int moved = 0, moved_elsewhere = 0;
    for (size_t i = 0; i < ks.size(); i++) {
        const std::string &now = *ring.owner(ks[i]);
        if (now != before[i]) {
            moved++;
            if (now != "s5") moved_elsewhere++;
        }
    }
    CHECK(moved < 6000);          // ideal 1/5 = 4000
    CHECK_EQ(moved_elsewhere, 0); // keys only ever move to the new node

    ModuloRouter m4(nodes);
    nodes.push_back("s5");
    ModuloRouter m5(nodes);
    int mod_moved = 0;
    for (const auto &k : ks) mod_moved += (m4.owner(k) != m5.owner(k));
    CHECK(mod_moved > 14000);  // ~4/5 = 16000
}

TEST(ring_removing_a_node_moves_only_its_keys) {
    auto ks = keys(20000);
    HashRing ring(100);
    for (const char *s : {"s1", "s2", "s3", "s4"}) ring.add_node(s);
    std::vector<std::string> before;
    for (const auto &k : ks) before.push_back(*ring.owner(k));
    ring.remove_node("s2");
    CHECK_EQ(ring.node_count(), size_t(3));
    bool only_s2_moved = true;
    for (size_t i = 0; i < ks.size(); i++) {
        if (*ring.owner(ks[i]) != before[i] && before[i] != "s2") only_s2_moved = false;
    }
    CHECK(only_s2_moved);
}

TEST(ring_vnodes_improve_balance) {
    auto ks = keys(50000);
    auto max_share = [&](int vnodes) {
        HashRing ring(vnodes);
        for (const char *s : {"s1", "s2", "s3", "s4", "s5"}) ring.add_node(s);
        std::map<std::string, int> c;
        for (const auto &k : ks) c[*ring.owner(k)]++;
        int mx = 0;
        for (auto &kv : c) mx = std::max(mx, kv.second);
        return double(mx) / ks.size();
    };
    double one = max_share(1), many = max_share(200);
    CHECK(many < one);
    CHECK(many < 0.26);  // ideal 0.20
}
