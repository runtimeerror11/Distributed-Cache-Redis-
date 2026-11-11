#include "check.h"
#include "distcache/cluster/topology.h"

using namespace distcache;

TEST(topology_parse_and_serialize) {
    std::string text =
        "# comment\n"
        "epoch 3\n"
        "vnodes 50\n"
        "\n"
        "shard s1 127.0.0.1:7101 127.0.0.1:7102\n"
        "shard s2 127.0.0.1:7201\n";
    Topology t;
    std::string err;
    CHECK(Topology::parse(text, &t, &err));
    CHECK_EQ(t.epoch, uint64_t(3));
    CHECK_EQ(t.vnodes, 50);
    CHECK_EQ(t.shards.size(), size_t(2));
    CHECK(t.find("s1") && t.find("s1")->replica == "127.0.0.1:7102");
    CHECK(t.find("s2") && t.find("s2")->replica.empty());
    CHECK(t.find("s3") == nullptr);

    Topology again;
    CHECK(Topology::parse(t.serialize(), &again, &err));
    CHECK(again.serialize() == t.serialize());
    CHECK_EQ(t.build_ring().node_count(), size_t(2));
}

TEST(topology_rejects_bad_maps) {
    Topology t;
    std::string err;
    CHECK(!Topology::parse("", &t, &err));
    CHECK(!Topology::parse("shard s1 nohost\n", &t, &err));
    CHECK(!Topology::parse("shard s1 h:1\nshard s1 h:2\n", &t, &err));
    CHECK(!Topology::parse("shard s1 h:1 h:1\n", &t, &err));
    CHECK(!Topology::parse("bogus 1\nshard s1 h:1\n", &t, &err));
    CHECK(!Topology::parse("epoch 0\nshard s1 h:1\n", &t, &err));
}
