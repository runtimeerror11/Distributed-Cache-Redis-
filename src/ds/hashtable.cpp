#include "distcache/ds/hashtable.h"

#include <cassert>
#include <cstdlib>

namespace distcache {

static const size_t k_max_load_factor = 8;   // chaining: several keys per slot is fine
static const size_t k_rehashing_work = 128;  // keys moved per operation while resizing

static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);  // power of 2
    // calloc: large arrays come from mmap() and are zeroed lazily, page by page,
    // so creating a big table does not cost O(N) up front.
    htab->tab = static_cast<HNode **>(calloc(n, sizeof(HNode *)));
    htab->mask = n - 1;
    htab->size = 0;
}

// Insert at the front of the slot's list: O(1).
static void h_insert(HTab *htab, HNode *node) {
    size_t pos = node->hcode & htab->mask;
    node->next = htab->tab[pos];
    htab->tab[pos] = node;
    htab->size++;
}

// Returns the address of the pointer that points to the matching node
// (either the slot itself or the previous node's `next`). Deletion then needs
// no special case for the first node of a chain.
static HNode **h_lookup(HTab *htab, HNode *key, HNodeEq eq) {
    if (!htab->tab) return nullptr;
    size_t pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[pos];
    for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    htab->size--;
    return node;
}

// Move a bounded number of keys from `older` to `newer`.
static void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;
    while (nwork < k_rehashing_work && hmap->older.size > 0) {
        HNode **from = &hmap->older.tab[hmap->migrate_pos];
        if (!*from) {
            hmap->migrate_pos++;  // empty slot, move on
            continue;
        }
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTab{};
    }
}

static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.tab == nullptr);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
    hmap->migrate_pos = 0;
}

HNode *hm_lookup(HMap *hmap, HNode *key, HNodeEq eq) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (!from) from = h_lookup(&hmap->older, key, eq);
    return from ? *from : nullptr;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.tab) h_init(&hmap->newer, 4);
    h_insert(&hmap->newer, node);  // new keys always go to the newer table
    if (!hmap->older.tab) {        // never start a resize while one is running
        size_t threshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size >= threshold) hm_trigger_rehashing(hmap);
    }
    hm_help_rehashing(hmap);
}

HNode *hm_delete(HMap *hmap, HNode *key, HNodeEq eq) {
    hm_help_rehashing(hmap);
    if (HNode **from = h_lookup(&hmap->newer, key, eq)) return h_detach(&hmap->newer, from);
    if (HNode **from = h_lookup(&hmap->older, key, eq)) return h_detach(&hmap->older, from);
    return nullptr;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

size_t hm_size(HMap *hmap) { return hmap->newer.size + hmap->older.size; }

static bool h_foreach(HTab *htab, bool (*f)(HNode *, void *), void *arg) {
    for (size_t i = 0; htab->tab && i <= htab->mask; i++) {
        for (HNode *node = htab->tab[i]; node != nullptr;) {
            HNode *next = node->next;  // saved before the callback runs
            if (!f(node, arg)) return false;
            node = next;
        }
    }
    return true;
}

void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {
    if (h_foreach(&hmap->newer, f, arg)) h_foreach(&hmap->older, f, arg);
}

}  // namespace distcache
