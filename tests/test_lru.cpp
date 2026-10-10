#include <gtest/gtest.h>

#include "distcache/store/lru_policy.h"

using namespace distcache;

TEST(Lru, DISABLED_EvictsLeastRecentlyUsed) {
    LruPolicy lru;
    lru.touch("a");
    lru.touch("b");
    lru.touch("a");  // "b" is now least recently used
    EXPECT_EQ(lru.victim(), "b");
}

TEST(Lru, EmptyHasNoVictim) {
    LruPolicy lru;
    EXPECT_FALSE(lru.victim().has_value());
}
