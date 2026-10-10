#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace distcache {

// Minimal TCP server. Reads RESP commands from each client and writes back replies.
// Start simple (thread per connection), later switch to an epoll event loop like Redis.
class TcpServer {
public:
    using Handler = std::function<std::string(const std::vector<std::string>&)>;

    TcpServer(std::uint16_t port, Handler handler);
    void run();   // blocks
    void stop();

private:
    std::uint16_t port_;
    Handler handler_;
    std::atomic<bool> running_{false};
};

}  // namespace distcache
