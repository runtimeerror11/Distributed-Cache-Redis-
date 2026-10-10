#include "distcache/server/tcp_server.h"

#include <iostream>

namespace distcache {

TcpServer::TcpServer(std::uint16_t port, Handler handler)
    : port_(port), handler_(std::move(handler)) {}

void TcpServer::run() {
    running_ = true;
    std::cout << "distcache: TCP server not implemented yet (port " << port_ << ")\n";
    // TODO(stage 1): socket() -> bind() -> listen() -> accept() loop.
    // For each client: read into a buffer, resp::parse_command(), handler_(args), write reply.
}

void TcpServer::stop() { running_ = false; }

}  // namespace distcache
