#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace distcache {

// "host:port" -> host, port. Returns false on malformed input.
bool parse_addr(const std::string &addr, std::string *host, uint16_t *port);

void set_nonblocking(int fd);

// Creates a non-blocking listening socket bound to 0.0.0.0:port. Returns -1 on error.
int listen_tcp(uint16_t port, std::string *err);

// Starts a non-blocking connect. Returns the fd (connection may still be in
// progress: wait for POLLOUT, then check connect_result()), or -1 on immediate failure.
int connect_nonblocking(const std::string &host, uint16_t port, std::string *err);

// After POLLOUT on a connecting socket: 0 if connected, otherwise the errno value.
int connect_result(int fd);

// Blocking connect with a timeout. Read/write timeouts (SO_RCVTIMEO/SO_SNDTIMEO)
// are set to io_timeout_ms so a dead peer can never hang the caller.
int connect_blocking(const std::string &host, uint16_t port, int timeout_ms, int io_timeout_ms,
                     std::string *err);

// Blocking helpers for clients: loop until all n bytes are transferred.
bool read_full(int fd, void *buf, size_t n);
bool write_all(int fd, const void *buf, size_t n);

}  // namespace distcache
