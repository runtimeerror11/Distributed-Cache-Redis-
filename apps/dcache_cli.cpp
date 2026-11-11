// dcache-cli: command-line client.
//
//   dcache-cli [--coord host:port] <command> [args...]   route by key through the cluster map
//   dcache-cli --node host:port <command> [args...]       talk to one node directly
//   dcache-cli [...]                                       no command: read commands from stdin
//
// Extra local commands (with --coord):
//   map            print the cluster map
//   where <key>    print the shard and primary that own <key>
//
// Exit status: 0 on a reply (including error replies), 1 if a node or the
// coordinator could not be reached, 2 on bad usage.
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "distcache/client/client.h"

using namespace distcache;

static std::vector<std::string> split_line(const std::string &line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false, have = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            have = true;
        } else if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (have) out.push_back(cur);
            cur.clear();
            have = false;
        } else {
            cur += ch;
            have = true;
        }
    }
    if (have) out.push_back(cur);
    return out;
}

static std::string lower(std::string s) {
    for (char &ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Returns false if the target could not be reached.
static bool run_command(ClusterClient &client, const std::string &node,
                        const std::vector<std::string> &cmd) {
    Value v;
    std::string name = lower(cmd[0]);
    if (!node.empty()) {
        v = client.call_node(node, cmd);
    } else if (name == "map" || name == "where") {
        std::string err;
        if (!client.refresh(&err)) {
            std::cout << "(err) coordinator: " << err << "\n";
            return false;
        }
        if (name == "map") {
            std::cout << client.topology().serialize();
        } else if (cmd.size() != 2) {
            std::cout << "(err) usage: where <key>\n";
        } else {
            std::cout << "shard " << client.shard_for(cmd[1]) << " primary "
                      << client.primary_for(cmd[1]) << "\n";
        }
        return true;
    } else if (is_key_command(name)) {
        v = client.call(cmd);
    } else {
        std::cout << "(err) '" << cmd[0]
                  << "' has no key to route by; use --node host:port to send it to one node\n";
        return true;
    }
    if (v.tag == TAG_STR && name == "info") {
        std::cout << v.str;  // already one field per line
    } else {
        std::cout << v.to_string() << "\n";
    }
    return !(v.is_err() && v.code == ERR_UNAVAILABLE);
}

int main(int argc, char **argv) {
    std::string coord = "127.0.0.1:7000", node;
    int i = 1;
    for (; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--coord" || a == "--node") && i + 1 < argc) {
            (a == "--coord" ? coord : node) = argv[++i];
        } else if (a == "-h" || a == "--help") {
            std::cout << "usage: dcache-cli [--coord host:port | --node host:port] "
                         "[command args...]\n";
            return 0;
        } else {
            break;
        }
    }
    ClusterClient client(coord);

    if (i < argc) {
        std::vector<std::string> cmd(argv + i, argv + argc);
        return run_command(client, node, cmd) ? 0 : 1;
    }

    // Interactive / scripted mode: one command per line.
    bool all_ok = true;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> cmd = split_line(line);
        if (cmd.empty()) continue;
        if (lower(cmd[0]) == "quit" || lower(cmd[0]) == "exit") break;
        all_ok = run_command(client, node, cmd) && all_ok;
    }
    return all_ok ? 0 : 1;
}
