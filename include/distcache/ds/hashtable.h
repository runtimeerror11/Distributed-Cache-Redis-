#pragma once
#include <cstddef>
#include <cstdint>

namespace distcache {

// Intrusive chaining hashtable with progressive rehashing.
//
// Intrusive: the user's struct embeds an HNode; the table only links HNodes and
// never allocates or moves them, so pointers to entries stay valid forever
// (other structures, like the LRU list and the TTL heap, can point into them).
//
// Progressive rehashing: when the load factor is reached, the current table
// becomes `older`, an empty table twice the size becomes `newer`, and every
// operation moves a bounded number of keys. No single request pays O(N).

struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;  // cached hash of the key
};

// Fixed-size table. Size is a power of 2, so `hcode & mask` picks the slot.
struct HTab {
    HNode **tab = nullptr;
    size_t mask = 0;
    size_t size = 0;
};

// Resizable map built from two fixed-size tables.
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;
};

using HNodeEq = bool (*)(HNode *, HNode *);

HNode *hm_lookup(HMap *hmap, HNode *key, HNodeEq eq);
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, HNodeEq eq);
void hm_clear(HMap *hmap);  // frees the slot arrays; nodes belong to the caller
size_t hm_size(HMap *hmap);
// Visits every node in both tables. The callback must not modify the map.
// Return false from the callback to stop early.
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);

}  // namespace distcache
