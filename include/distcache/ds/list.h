#pragma once
#include <cstddef>

namespace distcache {

// Intrusive circular doubly linked list with a dummy head node.
// The head is not a member: an empty list is a head pointing to itself, so
// insert and detach never need NULL checks or empty-list special cases.
//   head.next = front (oldest), head.prev = back (newest)
struct DList {
    DList *prev = nullptr;
    DList *next = nullptr;
};

inline void dlist_init(DList *node) { node->prev = node->next = node; }

inline bool dlist_empty(const DList *node) { return node->next == node; }

// Unlink a node from wherever it is: O(1), no search.
inline void dlist_detach(DList *node) {
    DList *prev = node->prev;
    DList *next = node->next;
    prev->next = next;
    next->prev = prev;
    node->prev = node->next = nullptr;
}

inline bool dlist_linked(const DList *node) { return node->next != nullptr; }

// Insert `rookie` just before `target`. With target = &head this appends at the back.
inline void dlist_insert_before(DList *target, DList *rookie) {
    DList *prev = target->prev;
    prev->next = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}

// Gets the struct that contains a member, given a pointer to that member.
#define container_of(ptr, T, member) \
    reinterpret_cast<T *>(reinterpret_cast<char *>(ptr) - offsetof(T, member))

}  // namespace distcache
