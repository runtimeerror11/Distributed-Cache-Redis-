#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "distcache/common/time.h"
#include "distcache/ds/hashtable.h"
#include "distcache/ds/heap.h"
#include "distcache/ds/list.h"

namespace distcache {

// One key. The same object is linked into three structures at once (intrusive):
//   hnode    -> the hashtable (lookup by key)
//   lru      -> the LRU list (eviction order)
//   heap_idx -> its position in the TTL heap, or k_heap_none if it has no TTL
// Entries are allocated individually and never move in memory, which is what
// lets the LRU list and the heap hold pointers into them.
struct Entry {
    HNode hnode;
    DList lru;
    size_t heap_idx = k_heap_none;
    std::string key;
    std::string val;
};

enum class DeleteReason { Expired, Evicted };

struct StoreStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t expired = 0;
};

// The in-memory key-value store of one node. Not thread-safe by design: the
// node runs a single-threaded event loop, so every command executes alone and
// is atomic without locks.
//
// Expiry:   lazy (checked on every access) + active (min-heap of deadlines,
//           processed by the event loop with a cap on work per iteration).
// Eviction: exact LRU under a byte budget (key + value + fixed overhead).
//
// Authoritative mode (primary) vs non-authoritative (replica):
// only the primary deletes keys on its own (expiry, eviction) and reports those
// deletions through the delete hook so they can be replicated. A replica keeps
// the same structures but only applies what its primary sends, so it stays an
// exact copy; expired keys are hidden from reads but not removed.
class Store {
public:
    using Clock = uint64_t (*)();
    using DeleteHook = std::function<void(const std::string &key, DeleteReason reason)>;

    // Accounted overhead per entry (node, list links, heap index, string headers).
    static const size_t k_entry_overhead = 64;

    explicit Store(size_t max_bytes, Clock clock = &wall_ms);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // ---- client operations (lazy expiry applies) ----
    // Returns nullptr when missing or expired. The pointer is valid until the next mutation.
    const std::string *get(const std::string &key);
    void set(const std::string &key, const std::string &val);  // clears any TTL, like Redis
    bool del(const std::string &key);
    // ttl_ms <= 0 deletes the key. Returns false if the key does not exist.
    bool expire(const std::string &key, int64_t ttl_ms);
    bool persist(const std::string &key);  // remove the TTL
    int64_t pttl(const std::string &key);  // ms left; -1 = no TTL; -2 = no such key
    // Absolute deadline of a key (wall-clock ms), 0 if it has none or does not exist.
    uint64_t deadline(const std::string &key);

    // ---- replication: apply exactly what the primary decided ----
    // deadline_ms is an absolute wall-clock time; 0 means no TTL.
    void apply_set(const std::string &key, const std::string &val, uint64_t deadline_ms);
    bool apply_del(const std::string &key);
    bool apply_expire_at(const std::string &key, uint64_t deadline_ms);

    // ---- maintenance ----
    // Active expiry: deletes up to max_work expired keys. Primary only.
    size_t process_expired(size_t max_work);
    // Nearest TTL deadline (wall-clock ms), or 0 if no key has a TTL.
    uint64_t next_deadline() const;
    // Visits live entries. The callback must not modify the store.
    void foreach(const std::function<void(const Entry &)> &f);
    uint64_t deadline_of(const Entry &e) const;
    void clear();

    void set_authoritative(bool on) { authoritative_ = on; }
    bool authoritative() const { return authoritative_; }
    void set_delete_hook(DeleteHook hook) { hook_ = std::move(hook); }

    size_t size();
    size_t used_bytes() const { return used_bytes_; }
    size_t max_bytes() const { return max_bytes_; }
    const StoreStats &stats() const { return stats_; }

private:
    Entry *find(const std::string &key);
    Entry *find_live(const std::string &key);  // applies lazy expiry
    bool is_expired(const Entry *e) const;
    Entry *insert_new(const std::string &key, const std::string &val);
    void replace_value(Entry *e, const std::string &val);
    void touch(Entry *e);
    void set_deadline(Entry *e, uint64_t deadline_ms);  // 0 removes the TTL
    void remove(Entry *e);                              // unlink from everything and free
    void remove_internal(Entry *e, DeleteReason reason);
    void evict_if_needed(Entry *keep);
    static size_t entry_bytes(const Entry *e) {
        return e->key.size() + e->val.size() + k_entry_overhead;
    }

    HMap db_;
    DList lru_head_;  // front = least recently used, back = most recently used
    std::vector<HeapItem> heap_;
    size_t used_bytes_ = 0;
    size_t max_bytes_;
    Clock clock_;
    bool authoritative_ = true;
    DeleteHook hook_;
    StoreStats stats_;
};

}  // namespace distcache
