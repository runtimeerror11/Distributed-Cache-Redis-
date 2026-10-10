#pragma once
#include <optional>
#include <string>
#include <vector>

namespace distcache::resp {

// RESP (REdis Serialization Protocol) so that redis-cli / redis-benchmark can talk to a node.
// Spec: https://redis.io/docs/latest/develop/reference/protocol-spec/

// Parses one command (array of bulk strings) from `buffer`.
// Returns the arguments and sets `consumed` to the bytes used,
// or std::nullopt if the buffer does not yet hold a full command.
std::optional<std::vector<std::string>> parse_command(const std::string& buffer,
                                                      std::size_t& consumed);

std::string simple_string(const std::string& s);   // +OK\r\n
std::string error(const std::string& msg);         // -ERR msg\r\n
std::string integer(long long n);                  // :1\r\n
std::string bulk_string(const std::string& s);     // $5\r\nhello\r\n
std::string null_bulk();                           // $-1\r\n

}  // namespace distcache::resp
