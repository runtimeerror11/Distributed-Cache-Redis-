#include <gtest/gtest.h>

#include "distcache/cluster/hash_ring.h"

using namespace distcache;

TEST(HashRing, HashIsDeterministic) {
    EXPECT_EQ(HashRing::hash("key"), HashRing::hash("key"));
}

TEST(HashRing, DISABLED_EveryKeyHasOwner) {
    HashRing ring(100);
    ring.add_node("n1");
    ring.add_node("n2");
    for (int i = 0; i < 1000; ++i) EXPECT_TRUE(ring.owner("k" + std::to_string(i)).has_value());
}

TEST(HashRing, DISABLED_AddingNodeMovesFewKeys) {
    HashRing ring(100);
    for (int i = 0; i < 4; ++i) ring.add_node("n" + std::to_string(i));
    std::vector<NodeId> before;
    for (int i = 0; i < 10000; ++i) before.push_back(*ring.owner("k" + std::to_string(i)));
    ring.add_node("n4");
    int moved = 0;
    for (int i = 0; i < 10000; ++i) moved += (*ring.owner("k" + std::to_string(i)) != before[i]);
    EXPECT_LT(moved, 3000);  // ideal is ~1/5 = 2000
}
