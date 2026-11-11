#include "distcache/common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>

namespace distcache {

bool parse_addr(const std::string &addr, std::string *host, uint16_t *port) {
    size_t colon = addr.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= addr.size()) return false;
    char *end = nullptr;
    long p = std::strtol(addr.c_str() + colon + 1, &end, 10);
    if (*end != '\0' || p <= 0 || p > 65535) return false;
    *host = addr.substr(0, colon);
    *port = static_cast<uint16_t>(p);
    return true;
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool resolve_ipv4(const std::string &host, uint16_t port, sockaddr_in *out,
                         std::string *err) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &out->sin_addr) == 1) return true;

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        if (err) *err = "cannot resolve host " + host;
        return false;
    }
    out->sin_addr = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

int listen_tcp(uint16_t port, std::string *err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (err) *err = std::string("socket: ") + strerror(errno);
        return -1;
    }
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        if (err) *err = "bind port " + std::to_string(port) + ": " + strerror(errno);
        close(fd);
        return -1;
    }
    set_nonblocking(fd);
    if (listen(fd, SOMAXCONN) != 0) {
        if (err) *err = std::string("listen: ") + strerror(errno);
        close(fd);
        return -1;
    }
    return fd;
}

static void set_nodelay(int fd) {
    int val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
}

int connect_nonblocking(const std::string &host, uint16_t port, std::string *err) {
    sockaddr_in addr;
    if (!resolve_ipv4(host, port, &addr, err)) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (err) *err = std::string("socket: ") + strerror(errno);
        return -1;
    }
    set_nonblocking(fd);
    set_nodelay(fd);
    int rv = connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (rv != 0 && errno != EINPROGRESS) {
        if (err) *err = std::string("connect: ") + strerror(errno);
        close(fd);
        return -1;
    }
    return fd;
}

int connect_result(int fd) {
    int so_err = 0;
    socklen_t len = sizeof(so_err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) != 0) return errno;
    return so_err;
}

int connect_blocking(const std::string &host, uint16_t port, int timeout_ms, int io_timeout_ms,
                     std::string *err) {
    int fd = connect_nonblocking(host, port, err);
    if (fd < 0) return -1;

    // Wait for the connect to finish, but never longer than timeout_ms.
    pollfd pfd = {fd, POLLOUT, 0};
    int rv = poll(&pfd, 1, timeout_ms);
    int cerr = (rv == 1) ? connect_result(fd) : ETIMEDOUT;
    if (cerr != 0) {
        if (err) *err = std::string("connect: ") + strerror(cerr);
        close(fd);
        return -1;
    }

    // Back to blocking mode, with read/write timeouts.
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    timeval tv = {io_timeout_ms / 1000, (io_timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool read_full(int fd, void *buf, size_t n) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
        ssize_t rv = read(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return false;  // error, timeout (EAGAIN) or EOF
        p += rv;
        n -= static_cast<size_t>(rv);
    }
    return true;
}

bool write_all(int fd, const void *buf, size_t n) {
    const char *p = static_cast<const char *>(buf);
    while (n > 0) {
        ssize_t rv = send(fd, p, n, MSG_NOSIGNAL);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return false;
        p += rv;
        n -= static_cast<size_t>(rv);
    }
    return true;
}

}  // namespace distcache
