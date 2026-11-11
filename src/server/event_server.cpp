#include "distcache/server/event_server.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <climits>

#include "distcache/common/log.h"
#include "distcache/common/net.h"
#include "distcache/common/time.h"

namespace distcache {

static volatile sig_atomic_t g_stop = 0;

void EventServer::request_stop() { g_stop = 1; }

static void on_signal(int) { g_stop = 1; }

void EventServer::install_signal_handlers() {
    signal(SIGPIPE, SIG_IGN);  // a write to a closed socket must return EPIPE, not kill us
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, nullptr);  // no SA_RESTART: poll() returns EINTR
    sigaction(SIGTERM, &sa, nullptr);
}

EventServer::~EventServer() {
    for (size_t i = 0; i < fd2conn_.size(); i++) {
        Conn *c = fd2conn_[i];
        if (!c) continue;
        close(c->fd);
        delete c;
    }
    if (listen_fd_ >= 0) close(listen_fd_);
}

bool EventServer::listen_now(std::string *err) {
    listen_fd_ = listen_tcp(port_, err);
    return listen_fd_ >= 0;
}

static void put_conn(std::vector<Conn *> &fd2conn, Conn *c) {
    if (fd2conn.size() <= size_t(c->fd)) fd2conn.resize(size_t(c->fd) + 1, nullptr);
    fd2conn[size_t(c->fd)] = c;
}

void EventServer::accept_new() {
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                DC_LOG("accept: %s", strerror(errno));
            }
            return;
        }
        set_nonblocking(fd);
        Conn *c = new Conn();
        c->fd = fd;
        c->last_active_ms = mono_ms();
        dlist_insert_before(&idle_list_, &c->idle_node);  // newest goes to the back
        put_conn(fd2conn_, c);
        num_clients_++;
    }
}

Conn *EventServer::connect_peer(const std::string &host, uint16_t port, std::string *err) {
    int fd = connect_nonblocking(host, port, err);
    if (fd < 0) return nullptr;
    Conn *c = new Conn();
    c->fd = fd;
    c->outbound = true;
    c->connecting = true;
    c->want_read = false;
    c->want_write = true;  // POLLOUT tells us when the connect finishes
    c->idle_exempt = true;
    put_conn(fd2conn_, c);
    return c;
}

void EventServer::send_request(Conn *conn, const std::vector<std::string> &cmd) {
    encode_request(conn->outgoing, cmd);
    conn->want_write = true;
}

void EventServer::set_idle_exempt(Conn *conn) {
    if (conn->idle_exempt) return;
    conn->idle_exempt = true;
    if (dlist_linked(&conn->idle_node)) dlist_detach(&conn->idle_node);
}

void EventServer::destroy(Conn *conn) {
    if (conn->outbound) on_closed(conn);
    close(conn->fd);
    fd2conn_[size_t(conn->fd)] = nullptr;
    if (dlist_linked(&conn->idle_node)) dlist_detach(&conn->idle_node);
    if (!conn->outbound) num_clients_--;
    delete conn;
}

void EventServer::destroy_closed() {
    for (size_t i = 0; i < fd2conn_.size(); i++) {
        Conn *c = fd2conn_[i];
        if (c && c->want_close) destroy(c);
    }
}

void EventServer::finish_connect(Conn *conn) {
    int err = connect_result(conn->fd);
    if (err != 0) {
        conn->want_close = true;
        return;
    }
    conn->connecting = false;
    conn->want_read = true;
    conn->want_write = !conn->outgoing.empty();
    on_connected(conn);
}

// Process one complete request from the input buffer, if there is one.
bool EventServer::try_one_request(Conn *conn) {
    uint32_t len = 0;
    FrameStatus st = peek_frame(conn->incoming.data(), conn->incoming.size(), &len);
    if (st == FrameStatus::Incomplete) return false;
    if (st == FrameStatus::TooBig) {
        DC_LOG("request too long, closing connection");
        conn->want_close = true;
        return false;
    }
    std::vector<std::string> cmd;
    if (!parse_request(conn->incoming.data() + 4, len, cmd)) {
        DC_LOG("bad request, closing connection");
        conn->want_close = true;
        return false;
    }
    // The response is written directly into the output buffer, framed by a
    // length header reserved here and filled in afterwards.
    size_t header = response_begin(conn->outgoing);
    handle_request(conn, cmd, conn->outgoing);
    response_end(conn->outgoing, header);
    conn->incoming.consume(4 + len);
    return true;
}

bool EventServer::try_one_response(Conn *conn) {
    uint32_t len = 0;
    FrameStatus st = peek_frame(conn->incoming.data(), conn->incoming.size(), &len);
    if (st == FrameStatus::Incomplete) return false;
    Value v;
    if (st == FrameStatus::TooBig || !parse_response(conn->incoming.data() + 4, len, &v)) {
        DC_LOG("bad response from peer, closing connection");
        conn->want_close = true;
        return false;
    }
    conn->incoming.consume(4 + len);
    on_response(conn, v);
    return !conn->want_close;
}

void EventServer::handle_read(Conn *conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    if (rv <= 0) {  // error, or EOF: the peer closed the connection
        conn->want_close = true;
        return;
    }
    conn->incoming.append(buf, size_t(rv));

    if (conn->outbound) {
        while (try_one_response(conn)) {
        }
        return;
    }
    // Pipelining: a client may send several requests at once; handle them all.
    while (!conn->want_close && try_one_request(conn)) {
    }
    if (!conn->outgoing.empty()) {
        // Stop reading until the replies are sent (backpressure), and try to
        // send right away instead of waiting for the next poll() round.
        conn->want_read = false;
        conn->want_write = true;
        handle_write(conn);
    }
}

void EventServer::handle_write(Conn *conn) {
    if (conn->outgoing.empty()) {
        conn->want_write = false;
        return;
    }
    ssize_t rv = send(conn->fd, conn->outgoing.data(), conn->outgoing.size(), MSG_NOSIGNAL);
    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    if (rv < 0) {
        conn->want_close = true;
        return;
    }
    conn->outgoing.consume(size_t(rv));  // a partial write leaves the rest for later
    if (conn->outgoing.empty()) {
        conn->want_write = false;
        if (!conn->outbound) conn->want_read = true;  // all replies sent: read again
    }
}

int EventServer::poll_timeout() {
    uint64_t now = mono_ms();
    uint64_t next = next_timer_ms();
    if (!dlist_empty(&idle_list_)) {
        Conn *c = container_of(idle_list_.next, Conn, idle_node);
        uint64_t idle_deadline = c->last_active_ms + idle_timeout_ms_;
        if (idle_deadline < next) next = idle_deadline;
    }
    uint64_t wait = (next == UINT64_MAX) ? 1000 : (next <= now ? 0 : next - now);
    // Capped so a stop request is noticed promptly even without traffic.
    return int(wait > 1000 ? 1000 : wait);
}

// The idle list is ordered by last activity, so only the front can be due.
void EventServer::process_idle() {
    uint64_t now = mono_ms();
    while (!dlist_empty(&idle_list_)) {
        Conn *c = container_of(idle_list_.next, Conn, idle_node);
        if (c->last_active_ms + idle_timeout_ms_ > now) break;
        dlist_detach(&c->idle_node);
        c->want_close = true;
    }
}

void EventServer::run() {
    std::vector<pollfd> poll_args;
    while (!g_stop) {
        poll_args.clear();
        poll_args.push_back(pollfd{listen_fd_, POLLIN, 0});
        for (Conn *c : fd2conn_) {
            if (!c) continue;
            pollfd pfd = {c->fd, POLLERR, 0};
            if (c->want_read) pfd.events |= POLLIN;
            if (c->want_write || c->connecting) pfd.events |= POLLOUT;
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), nfds_t(poll_args.size()), poll_timeout());
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) {
            DC_LOG("poll: %s", strerror(errno));
            break;
        }

        if (poll_args[0].revents) accept_new();

        for (size_t i = 1; i < poll_args.size(); i++) {
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) continue;
            Conn *c = fd2conn_[size_t(poll_args[i].fd)];
            if (!c) continue;
            if (c->connecting) {
                finish_connect(c);
            } else {
                if (!c->idle_exempt) {
                    // Activity: reset the idle timer by moving the conn to the back.
                    c->last_active_ms = mono_ms();
                    dlist_detach(&c->idle_node);
                    dlist_insert_before(&idle_list_, &c->idle_node);
                }
                if (ready & (POLLIN | POLLHUP)) handle_read(c);
                if ((ready & POLLOUT) && !c->want_close) handle_write(c);
                if (ready & POLLERR) c->want_close = true;
            }
            if (c->want_close) destroy(c);
        }

        process_idle();
        process_timers();
        destroy_closed();
    }
}

}  // namespace distcache
