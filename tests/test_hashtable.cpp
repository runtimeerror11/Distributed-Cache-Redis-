#include <string>
#include <vector>

#include "check.h"
#include "distcache/common/hash.h"
#include "distcache/ds/hashtable.h"
#include "distcache/ds/list.h"

using namespace distcache;

namespace {

struct Item {
    HNode node;
    std::string key;
    int val = 0;
};

bool item_eq(HNode *a, HNode *b) {
    return container_of(a, Item, node)->key == container_of(b, Item, node)->key;
}

Item *find(HMap &m, const std::string &key) {
    Item q;
    q.key = key;
    q.node.hcode = hash64(key);
    HNode *n = hm_lookup(&m, &q.node, &item_eq);
    return n ? container_of(n, Item, node) : nullptr;
}

}  // namespace

TEST(hashtable_insert_lookup_delete) {
    HMap m;
    std::vector<Item *> items;
    const int n = 20000;
    for (int i = 0; i < n; i++) {
        Item *it = new Item();
        it->key = "k" + std::to_string(i);
        it->val = i;
        it->node.hcode = hash64(it->key);
        hm_insert(&m, &it->node);
        items.push_back(it);
    }
    CHECK_EQ(hm_size(&m), size_t(n));
    // The table grew several times, progressively; every key must still be found.
    bool all_found = true;
    for (int i = 0; i < n; i++) {
        Item *it = find(m, "k" + std::to_string(i));
        if (!it || it->val != i) all_found = false;
    }
    CHECK(all_found);
    CHECK(find(m, "missing") == nullptr);

    for (int i = 0; i < n; i += 2) {
        Item q;
        q.key = "k" + std::to_string(i);
        q.node.hcode = hash64(q.key);
        HNode *removed = hm_delete(&m, &q.node, &item_eq);
        CHECK(removed == &items[size_t(i)]->node);
    }
    CHECK_EQ(hm_size(&m), size_t(n / 2));
    CHECK(find(m, "k0") == nullptr);
    CHECK(find(m, "k1") != nullptr);

    size_t visited = 0;
    hm_foreach(
        &m,
        [](HNode *, void *arg) {
            (*static_cast<size_t *>(arg))++;
            return true;
        },
        &visited);
    CHECK_EQ(visited, size_t(n / 2));

    hm_clear(&m);
    for (Item *it : items) delete it;
}

TEST(hashtable_nodes_never_move) {
    // Reference stability: resizing relinks nodes but never moves them.
    HMap m;
    std::vector<Item *> items;
    for (int i = 0; i < 5000; i++) {
        Item *it = new Item();
        it->key = "x" + std::to_string(i);
        it->node.hcode = hash64(it->key);
        hm_insert(&m, &it->node);
        items.push_back(it);
    }
    for (int i = 0; i < 5000; i++) CHECK(find(m, "x" + std::to_string(i)) == items[size_t(i)]);
    hm_clear(&m);
    for (Item *it : items) delete it;
}
