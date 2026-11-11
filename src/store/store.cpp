#include "distcache/store/store.h"

#include "distcache/common/hash.h"

namespace distcache {

// A lookup needs an HNode with the key's hash plus the key itself, but not a
// whole Entry. The table compares a stored Entry (lhs) with this (rhs).
struct LookupKey {
    HNode node;
    std::string key;
};

static bool key_eq(HNode *lhs, HNode *rhs) {
    Entry *e = container_of(lhs, Entry, hnode);
    LookupKey *k = container_of(rhs, LookupKey, node);
    return e->key == k->key;
}

// Used when we already hold the exact node: compare pointers, not strings.
static bool hnode_same(HNode *lhs, HNode *rhs) { return lhs == rhs; }

Store::Store(size_t max_bytes, Clock clock) : max_bytes_(max_bytes), clock_(clock) {
    dlist_init(&lru_head_);
}

Store::~Store() { clear(); }

// ---- internal helpers -------------------------------------------------------

Entry *Store::find(const std::string &key) {
    LookupKey k;
    k.key = key;
    k.node.hcode = hash64(key);
    HNode *node = hm_lookup(&db_, &k.node, &key_eq);
    return node ? container_of(node, Entry, hnode) : nullptr;
}

bool Store::is_expired(const Entry *e) const {
    return e->heap_idx != k_heap_none && heap_[e->heap_idx].val <= clock_();
}

// Lazy expiry: an expired key is never returned. The primary also deletes it
// right away (and reports it); a replica only hides it and waits for the
// primary's delete, so the two copies never diverge.
Entry *Store::find_live(const std::string &key) {
    Entry *e = find(key);
    if (e && is_expired(e)) {
        if (authoritative_) remove_internal(e, DeleteReason::Expired);
        return nullptr;
    }
    return e;
}

void Store::touch(Entry *e) {
    if (dlist_linked(&e->lru)) dlist_detach(&e->lru);
    dlist_insert_before(&lru_head_, &e->lru);  // move to the back = most recent
}

void Store::set_deadline(Entry *e, uint64_t deadline_ms) {
    if (deadline_ms == 0) {
        if (e->heap_idx != k_heap_none) {
            heap_delete(heap_, e->heap_idx);
            e->heap_idx = k_heap_none;
        }
    } else {
        // Insert, or update in place if the key already has a timer.
        heap_upsert(heap_, e->heap_idx, HeapItem{deadline_ms, &e->heap_idx});
    }
}

Entry *Store::insert_new(const std::string &key, const std::string &val) {
    Entry *e = new Entry();
    e->key = key;
    e->val = val;
    e->hnode.hcode = hash64(key);
    hm_insert(&db_, &e->hnode);
    touch(e);
    used_bytes_ += entry_bytes(e);
    return e;
}

void Store::replace_value(Entry *e, const std::string &val) {
    used_bytes_ -= e->val.size();
    e->val = val;
    used_bytes_ += e->val.size();
    touch(e);
}

// Every deletion path goes through here, so the entry is always unlinked from
// the hashtable, the LRU list and the heap before it is freed. Forgetting the
// heap would leave a pointer into freed memory that the heap later writes to.
void Store::remove(Entry *e) {
    hm_delete(&db_, &e->hnode, &hnode_same);
    dlist_detach(&e->lru);
    set_deadline(e, 0);
    used_bytes_ -= entry_bytes(e);
    delete e;
}

void Store::remove_internal(Entry *e, DeleteReason reason) {
    std::string key = hook_ ? e->key : std::string();
    remove(e);
    if (reason == DeleteReason::Expired) stats_.expired++;
    if (reason == DeleteReason::Evicted) stats_.evictions++;
    if (hook_) hook_(key, reason);
}

// Evict least recently used entries until the budget fits. `keep` (the entry
// being written) is never evicted: if it alone exceeds the budget it stays.
// Replicas never evict on their own; they apply the primary's deletions.
void Store::evict_if_needed(Entry *keep) {
    if (!authoritative_) return;
    while (used_bytes_ > max_bytes_ && !dlist_empty(&lru_head_)) {
        Entry *victim = container_of(lru_head_.next, Entry, lru);
        if (victim == keep) break;
        remove_internal(victim, DeleteReason::Evicted);
    }
}

// ---- client operations ------------------------------------------------------

const std::string *Store::get(const std::string &key) {
    Entry *e = find_live(key);
    if (!e) {
        stats_.misses++;
        return nullptr;
    }
    stats_.hits++;
    touch(e);
    return &e->val;
}

void Store::set(const std::string &key, const std::string &val) {
    Entry *e = find(key);
    if (e) {
        replace_value(e, val);
        set_deadline(e, 0);
    } else {
        e = insert_new(key, val);
    }
    evict_if_needed(e);
}

bool Store::del(const std::string &key) {
    Entry *e = find_live(key);
    if (!e) return false;
    remove(e);
    return true;
}

bool Store::expire(const std::string &key, int64_t ttl_ms) {
    Entry *e = find_live(key);
    if (!e) return false;
    if (ttl_ms <= 0) {
        remove(e);  // an expiry time in the past deletes the key
    } else {
        set_deadline(e, clock_() + uint64_t(ttl_ms));
    }
    return true;
}

bool Store::persist(const std::string &key) {
    Entry *e = find_live(key);
    if (!e || e->heap_idx == k_heap_none) return false;
    set_deadline(e, 0);
    return true;
}

int64_t Store::pttl(const std::string &key) {
    Entry *e = find_live(key);
    if (!e) return -2;
    if (e->heap_idx == k_heap_none) return -1;
    uint64_t deadline = heap_[e->heap_idx].val, now = clock_();
    return deadline > now ? int64_t(deadline - now) : 0;
}

uint64_t Store::deadline(const std::string &key) {
    Entry *e = find(key);
    return e ? deadline_of(*e) : 0;
}

// ---- replication ------------------------------------------------------------

void Store::apply_set(const std::string &key, const std::string &val, uint64_t deadline_ms) {
    Entry *e = find(key);
    if (e) {
        replace_value(e, val);
    } else {
        e = insert_new(key, val);
    }
    set_deadline(e, deadline_ms);
    evict_if_needed(e);
}

bool Store::apply_del(const std::string &key) {
    Entry *e = find(key);
    if (!e) return false;
    remove(e);
    return true;
}

bool Store::apply_expire_at(const std::string &key, uint64_t deadline_ms) {
    Entry *e = find(key);
    if (!e) return false;
    set_deadline(e, deadline_ms);
    return true;
}

// ---- maintenance ------------------------------------------------------------

size_t Store::process_expired(size_t max_work) {
    if (!authoritative_) return 0;
    uint64_t now = clock_();
    size_t n = 0;
    // heap_[0] is always the nearest deadline. Removing the entry also removes
    // its heap item, so the next nearest moves to the top.
    while (!heap_.empty() && heap_[0].val <= now && n < max_work) {
        Entry *e = container_of(heap_[0].ref, Entry, heap_idx);
        remove_internal(e, DeleteReason::Expired);
        n++;
    }
    return n;
}

uint64_t Store::next_deadline() const { return heap_.empty() ? 0 : heap_[0].val; }

uint64_t Store::deadline_of(const Entry &e) const {
    return e.heap_idx == k_heap_none ? 0 : heap_[e.heap_idx].val;
}

struct ForeachCtx {
    const Store *store;
    const std::function<void(const Entry &)> *f;
    uint64_t now;
};

void Store::foreach(const std::function<void(const Entry &)> &f) {
    ForeachCtx ctx{this, &f, clock_()};
    hm_foreach(
        &db_,
        [](HNode *node, void *arg) -> bool {
            ForeachCtx *c = static_cast<ForeachCtx *>(arg);
            const Entry *e = container_of(node, Entry, hnode);
            uint64_t deadline = c->store->deadline_of(*e);
            if (deadline == 0 || deadline > c->now) (*c->f)(*e);
            return true;
        },
        &ctx);
}

void Store::clear() {
    std::vector<Entry *> all;
    hm_foreach(
        &db_,
        [](HNode *node, void *arg) -> bool {
            static_cast<std::vector<Entry *> *>(arg)->push_back(container_of(node, Entry, hnode));
            return true;
        },
        &all);
    for (Entry *e : all) delete e;
    hm_clear(&db_);
    heap_.clear();
    dlist_init(&lru_head_);
    used_bytes_ = 0;
}

size_t Store::size() { return hm_size(&db_); }

}  // namespace distcache
