#include "check.h"
#include "distcache/ds/list.h"

using namespace distcache;

namespace {
struct Node {
    int v = 0;
    DList link;
};
}  // namespace

TEST(dlist_dummy_head_and_move_to_back) {
    DList head;
    dlist_init(&head);
    CHECK(dlist_empty(&head));

    Node a, b, c;
    a.v = 1;
    b.v = 2;
    c.v = 3;
    dlist_insert_before(&head, &a.link);  // append to back
    dlist_insert_before(&head, &b.link);
    dlist_insert_before(&head, &c.link);
    CHECK_EQ(container_of(head.next, Node, link)->v, 1);  // front = oldest

    // Touch `a`: move it to the back. `b` becomes the front (least recent).
    dlist_detach(&a.link);
    dlist_insert_before(&head, &a.link);
    CHECK_EQ(container_of(head.next, Node, link)->v, 2);
    CHECK_EQ(container_of(head.prev, Node, link)->v, 1);

    dlist_detach(&b.link);
    dlist_detach(&c.link);
    dlist_detach(&a.link);
    CHECK(dlist_empty(&head));
}
