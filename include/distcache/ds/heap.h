#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace distcache {

// Array-based binary min-heap used for TTL timers.
// Only the minimum (nearest deadline) is ever needed, so a heap is enough:
// O(1) peek, O(log N) insert / update / delete, no per-node pointers.
//
// Each item points back to an index field inside its owner (`ref`). The heap
// writes the item's current position there every time it moves the item, so
// the owner can find, update or remove its own timer in O(1) without the heap
// knowing anything about the owner's type.
struct HeapItem {
    uint64_t val = 0;       // the deadline
    size_t *ref = nullptr;  // points to the owner's index field
};

const size_t k_heap_none = static_cast<size_t>(-1);  // "not in the heap"

// Restore the heap property for the item at `pos` (moves it up or down).
void heap_update(HeapItem *a, size_t pos, size_t len);
// Remove the item at `pos`: the last item fills the hole, then is fixed up.
void heap_delete(std::vector<HeapItem> &a, size_t pos);
// Overwrite the item at `pos`, or append if `pos` is not a valid index.
void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem t);

}  // namespace distcache
