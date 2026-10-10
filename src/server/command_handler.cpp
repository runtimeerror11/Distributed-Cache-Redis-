#include "distcache/server/command_handler.h"

#include <algorithm>
#include <cctype>

#include "distcache/protocol/resp.h"

namespace distcache {

CommandHandler::CommandHandler(NodeId self, KvStore& store, OwnershipTable& ownership,
                               Replicator& replicator)
    : self_(std::move(self)), store_(store), ownership_(ownership), replicator_(replicator) {}

std::string CommandHandler::handle(const std::vector<std::string>& args) {
    if (args.empty()) return resp::error("empty command");

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "PING") return resp::simple_string("PONG");

    // TODO(stage 1): GET, SET, DEL, EXPIRE, TTL on store_.
    // TODO(stage 2): check ring ownership; reply -MOVED if another node owns the key.
    // TODO(stage 3): after a successful write on the primary, call replicator_.
    return resp::error("unknown or unimplemented command '" + args[0] + "'");
}

}  // namespace distcache
