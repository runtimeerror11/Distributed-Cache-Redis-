#include "distcache/cluster/topology.h"

#include <fstream>
#include <set>
#include <sstream>

#include "distcache/common/net.h"

namespace distcache {

const ShardInfo *Topology::find(const std::string &shard_id) const {
    for (const ShardInfo &s : shards) {
        if (s.id == shard_id) return &s;
    }
    return nullptr;
}

HashRing Topology::build_ring() const {
    HashRing ring(vnodes);
    for (const ShardInfo &s : shards) ring.add_node(s.id);
    return ring;
}

std::string Topology::serialize() const {
    std::ostringstream os;
    os << "epoch " << epoch << "\n";
    os << "vnodes " << vnodes << "\n";
    for (const ShardInfo &s : shards) {
        os << "shard " << s.id << " " << s.primary;
        if (!s.replica.empty()) os << " " << s.replica;
        os << "\n";
    }
    return os.str();
}

static bool valid_addr(const std::string &a) {
    std::string host;
    uint16_t port = 0;
    return parse_addr(a, &host, &port);
}

bool Topology::parse(const std::string &text, Topology *out, std::string *err) {
    Topology t;
    std::istringstream in(text);
    std::string line;
    std::set<std::string> ids, addrs;
    int lineno = 0;
    auto fail = [&](const std::string &msg) {
        if (err) *err = "line " + std::to_string(lineno) + ": " + msg;
        return false;
    };
    while (std::getline(in, line)) {
        lineno++;
        std::istringstream ls(line);
        std::string word;
        if (!(ls >> word) || word[0] == '#') continue;
        if (word == "epoch") {
            if (!(ls >> t.epoch) || t.epoch == 0) return fail("bad epoch");
        } else if (word == "vnodes") {
            if (!(ls >> t.vnodes) || t.vnodes < 1) return fail("bad vnodes");
        } else if (word == "shard") {
            ShardInfo s;
            if (!(ls >> s.id >> s.primary)) return fail("expected: shard <id> <primary> [replica]");
            ls >> s.replica;  // optional
            if (!ids.insert(s.id).second) return fail("duplicate shard id " + s.id);
            if (!valid_addr(s.primary)) return fail("bad primary address " + s.primary);
            if (!addrs.insert(s.primary).second) return fail("address used twice " + s.primary);
            if (!s.replica.empty()) {
                if (!valid_addr(s.replica)) return fail("bad replica address " + s.replica);
                if (!addrs.insert(s.replica).second) return fail("address used twice " + s.replica);
            }
            t.shards.push_back(s);
        } else {
            return fail("unknown directive '" + word + "'");
        }
    }
    if (t.shards.empty()) {
        if (err) *err = "no shards defined";
        return false;
    }
    *out = t;
    return true;
}

bool Topology::load_file(const std::string &path, Topology *out, std::string *err) {
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), out, err);
}

}  // namespace distcache
