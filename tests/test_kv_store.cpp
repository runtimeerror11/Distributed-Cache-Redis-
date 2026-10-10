// Tests are prefixed DISABLED_ until the feature is implemented.
// Remove the prefix as you complete each stage so CI keeps checking it.
#include <gtest/gtest.h>
#include <thread>

#include "distcache/store/kv_store.h"

using namespace distcache;

TEST(KvStore, DISABLED_SetThenGet) {
    KvStore s(1 << 20);
    s.set("a", "1");
    EXPECT_EQ(s.get("a"), "1");
}

TEST(KvStore, DISABLED_DelRemovesKey) {
    KvStore s(1 << 20);
    s.set("a", "1");
    EXPECT_TRUE(s.del("a"));
    EXPECT_FALSE(s.get("a").has_value());
}

TEST(KvStore, DISABLED_LazyExpiry) {
    KvStore s(1 << 20);
    s.set("a", "1");
    s.expire("a", std::chrono::seconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(s.get("a").has_value());
}

TEST(KvStore, EmptyStoreIsEmpty) {
    KvStore s(1 << 20);
    EXPECT_EQ(s.size(), 0u);
}
