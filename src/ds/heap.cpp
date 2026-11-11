#include "distcache/ds/heap.h"

namespace distcache {

static size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }
static size_t heap_left(size_t i) { return i * 2 + 1; }
static size_t heap_right(size_t i) { return i * 2 + 2; }

// The moving item is held aside in `t`; others shift into the hole, and every
// placement tells the owner its new position through `ref`.
static void heap_up(HeapItem *a, size_t pos) {
    HeapItem t = a[pos];
    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        a[pos] = a[heap_parent(pos)];
        *a[pos].ref = pos;
        pos = heap_parent(pos);
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

static void heap_down(HeapItem *a, size_t pos, size_t len) {
    HeapItem t = a[pos];
    while (true) {
        size_t l = heap_left(pos), r = heap_right(pos);
        size_t min_pos = pos;
        uint64_t min_val = t.val;
        if (l < len && a[l].val < min_val) {
            min_pos = l;
            min_val = a[l].val;
        }
        if (r < len && a[r].val < min_val) min_pos = r;
        if (min_pos == pos) break;
        a[pos] = a[min_pos];
        *a[pos].ref = pos;
        pos = min_pos;
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

void heap_update(HeapItem *a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    } else {
        heap_down(a, pos, len);
    }
}

void heap_delete(std::vector<HeapItem> &a, size_t pos) {
    a[pos] = a.back();
    a.pop_back();
    if (pos < a.size()) heap_update(a.data(), pos, a.size());
}

void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem t) {
    if (pos < a.size()) {
        a[pos] = t;  // update an existing item
    } else {
        pos = a.size();
        a.push_back(t);  // add a new item
    }
    heap_update(a.data(), pos, a.size());
}

}  // namespace distcache
