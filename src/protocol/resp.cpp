#include "distcache/protocol/resp.h"

namespace distcache::resp {

std::optional<std::vector<std::string>> parse_command(const std::string& buffer,
                                                      std::size_t& consumed) {
    // TODO(stage 1): parse "*<n>\r\n" followed by n x "$<len>\r\n<data>\r\n".
    // Return nullopt if the buffer is incomplete (TCP may deliver partial commands).
    (void)buffer;
    consumed = 0;
    return std::nullopt;
}

std::string simple_string(const std::string& s) { return "+" + s + "\r\n"; }
std::string error(const std::string& msg) { return "-ERR " + msg + "\r\n"; }
std::string integer(long long n) { return ":" + std::to_string(n) + "\r\n"; }
std::string bulk_string(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}
std::string null_bulk() { return "$-1\r\n"; }

}  // namespace distcache::resp
