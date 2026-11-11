#include <algorithm>
#include <random>
#include <vector>

#include "check.h"
#include "distcache/ds/heap.h"

using namespace distcache;

namespace {

struct Timer {
    uint64_t deadline = 0;
    size_t heap_idx = k_heap_none;
};

bool heap_ok(const std::vector<HeapItem> &h) {
    for (size_t i = 1; i < h.size(); i++) {
        if (h[(i - 1) / 2].val > h[i].val) return false;
    }
    for (size_t i = 0; i < h.size(); i++) {
        if (*h[i].ref != i) return false;  // every owner knows its position
    }
    return true;
}

}  // namespace

TEST(heap_keeps_min_on_top_and_back_references) {
    std::mt19937 rng(7);
    std::vector<Timer> timers(2000);
    std::vector<HeapItem> heap;
    for (Timer &t : timers) {
        t.deadline = rng() % 100000;
        heap_upsert(heap, t.heap_idx, HeapItem{t.deadline, &t.heap_idx});
    }
    CHECK(heap_ok(heap));

    // Update half, delete a quarter, using each owner's own index (O(1) lookup).
    for (size_t i = 0; i < timers.size(); i += 2) {
        timers[i].deadline = rng() % 100000;
        heap_upsert(heap, timers[i].heap_idx, HeapItem{timers[i].deadline, &timers[i].heap_idx});
    }
    for (size_t i = 1; i < timers.size(); i += 4) {
        heap_delete(heap, timers[i].heap_idx);
        timers[i].heap_idx = k_heap_none;
    }
    CHECK(heap_ok(heap));

    // Popping from the top yields deadlines in ascending order.
    uint64_t last = 0;
    bool sorted = true;
    while (!heap.empty()) {
        if (heap[0].val < last) sorted = false;
        last = heap[0].val;
        size_t *ref = heap[0].ref;
        heap_delete(heap, 0);
        *ref = k_heap_none;
    }
    CHECK(sorted);
}
