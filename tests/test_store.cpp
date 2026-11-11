#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "distcache/store/store.h"

using namespace distcache;

namespace {
uint64_t g_now = 1000000;
uint64_t fake_clock() { return g_now; }
const size_t kBig = 1u << 20;
}  // namespace

TEST(store_set_get_del) {
    Store s(kBig, fake_clock);
    CHECK(s.get("a") == nullptr);
    s.set("a", "1");
    CHECK(s.get("a") && *s.get("a") == "1");
    s.set("a", "2");  // overwrite: last write wins
    CHECK(*s.get("a") == "2");
    CHECK_EQ(s.size(), size_t(1));
    CHECK(s.del("a"));
    CHECK(!s.del("a"));
    CHECK(s.get("a") == nullptr);
    CHECK_EQ(s.used_bytes(), size_t(0));
    CHECK_EQ(s.stats().hits, uint64_t(3));
    CHECK_EQ(s.stats().misses, uint64_t(2));
}

TEST(store_ttl_lazy_expiry) {
    Store s(kBig, fake_clock);
    s.set("k", "v");
    CHECK_EQ(s.pttl("k"), int64_t(-1));
    CHECK_EQ(s.pttl("nope"), int64_t(-2));
    CHECK(s.expire("k", 5000));
    CHECK_EQ(s.pttl("k"), int64_t(5000));
    g_now += 4999;
    CHECK(s.get("k") != nullptr);
    g_now += 1;  // deadline reached
    CHECK(s.get("k") == nullptr);  // lazy: deleted on access
    CHECK_EQ(s.size(), size_t(0));
    CHECK_EQ(s.stats().expired, uint64_t(1));
    CHECK(!s.expire("nope", 1000));
}

TEST(store_set_clears_ttl_and_persist) {
    Store s(kBig, fake_clock);
    s.set("k", "v");
    s.expire("k", 1000);
    s.set("k", "v2");
    CHECK_EQ(s.pttl("k"), int64_t(-1));
    s.expire("k", 1000);
    CHECK(s.persist("k"));
    CHECK(!s.persist("k"));
    g_now += 5000;
    CHECK(s.get("k") != nullptr);
    CHECK(s.expire("k", 0));  // TTL <= 0 deletes
    CHECK(s.get("k") == nullptr);
}

TEST(store_active_expiry_with_work_cap) {
    Store s(kBig, fake_clock);
    for (int i = 0; i < 100; i++) {
        s.set("k" + std::to_string(i), "v");
        s.expire("k" + std::to_string(i), 100 + i);
    }
    s.set("keep", "v");
    CHECK_EQ(s.next_deadline(), g_now + 100);
    g_now += 1000;
    CHECK_EQ(s.process_expired(30), size_t(30));  // capped per call
    CHECK_EQ(s.process_expired(1000), size_t(70));
    CHECK_EQ(s.size(), size_t(1));
    CHECK_EQ(s.next_deadline(), uint64_t(0));
}

TEST(store_lru_eviction) {
    const size_t per = Store::k_entry_overhead + 2 + 10;  // key "kN" + 10-byte value
    Store s(per * 3, fake_clock);
    std::string v(10, 'x');
    s.set("k1", v);
    s.set("k2", v);
    s.set("k3", v);
    CHECK(s.get("k1") != nullptr);  // k1 becomes most recently used; k2 is now least
    s.set("k4", v);                 // over budget: evict the LRU entry
    CHECK(s.get("k2") == nullptr);
    CHECK(s.get("k1") != nullptr);
    CHECK(s.get("k3") != nullptr);
    CHECK(s.get("k4") != nullptr);
    CHECK_EQ(s.stats().evictions, uint64_t(1));
    CHECK(s.used_bytes() <= s.max_bytes());
}

TEST(store_delete_hook_reports_own_decisions) {
    Store s(Store::k_entry_overhead + 20, fake_clock);
    std::vector<std::pair<std::string, DeleteReason>> seen;
    s.set_delete_hook([&](const std::string &k, DeleteReason r) { seen.push_back({k, r}); });
    s.set("a", "1");
    s.set("b", "2");  // evicts "a"
    s.expire("b", 10);
    g_now += 20;
    s.process_expired(10);  // expires "b"
    CHECK_EQ(seen.size(), size_t(2));
    CHECK(seen[0].first == "a" && seen[0].second == DeleteReason::Evicted);
    CHECK(seen[1].first == "b" && seen[1].second == DeleteReason::Expired);
}

TEST(store_replica_never_deletes_on_its_own) {
    Store s(Store::k_entry_overhead + 4, fake_clock);
    s.set_authoritative(false);
    s.apply_set("a", "1", 0);
    s.apply_set("b", "2", g_now + 10);  // over budget, but replicas never evict
    CHECK_EQ(s.size(), size_t(2));
    CHECK_EQ(s.stats().evictions, uint64_t(0));
    g_now += 20;
    CHECK(s.get("b") == nullptr);         // expired keys are hidden from reads...
    CHECK_EQ(s.process_expired(10), size_t(0));
    CHECK_EQ(s.size(), size_t(2));        // ...but stay until the primary says delete
    CHECK(s.apply_del("b"));
    CHECK_EQ(s.size(), size_t(1));
}

TEST(store_apply_set_and_foreach) {
    Store s(kBig, fake_clock);
    s.apply_set("a", "1", g_now + 500);
    s.apply_set("b", "2", 0);
    s.apply_set("c", "3", g_now - 1);  // already expired: hidden from foreach
    CHECK_EQ(s.deadline("a"), g_now + 500);
    size_t n = 0;
    s.foreach([&](const Entry &) { n++; });
    CHECK_EQ(n, size_t(2));
    CHECK(s.apply_expire_at("a", 0));
    CHECK_EQ(s.pttl("a"), int64_t(-1));
    s.clear();
    CHECK_EQ(s.size(), size_t(0));
    CHECK_EQ(s.used_bytes(), size_t(0));
}
