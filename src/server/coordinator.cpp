#include "distcache/server/coordinator.h"

#include <cctype>

namespace distcache {

void Coordinator::handle_request(Conn *, std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.empty()) return out_err(out, ERR_BAD_ARG, "empty command");
    std::string name;
    for (char ch : cmd[0]) name += char(std::tolower(static_cast<unsigned char>(ch)));

    if (name == "ping") return out_str(out, "PONG");
    if (name == "getmap") return out_str(out, topo_.serialize());
    if (name == "epoch") return out_int(out, int64_t(topo_.epoch));
    out_err(out, ERR_UNKNOWN, "unknown command '" + cmd[0] + "'");
}

}  // namespace distcache
