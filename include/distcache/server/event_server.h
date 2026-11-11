#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "distcache/common/buffer.h"
#include "distcache/ds/list.h"
#include "distcache/protocol/protocol.h"

namespace distcache {

// One TCP connection handled by the event loop.
//  inbound  = a client connected to us; frames are requests, we write responses.
//  outbound = we connected to a peer (e.g. primary -> replica); we write requests,
//             frames we read are responses.
struct Conn {
    int fd = -1;
    bool outbound = false;
    bool connecting = false;  // non-blocking connect still in progress
    bool want_read = true;
    bool want_write = false;
    bool want_close = false;
    bool idle_exempt = false;  // e.g. the replication link: quiet is normal
    Buffer incoming;
    Buffer outgoing;
    uint64_t last_active_ms = 0;
    DList idle_node;
};

// Single-threaded, non-blocking event loop around poll().
//
// One thread serves every connection, so command execution needs no locks and
// each command is atomic. Nothing in the loop may block: sockets are
// non-blocking, partial reads are buffered until a whole message has arrived,
// partial writes stay in the output buffer until the socket is writable again.
//
// Timers: poll()'s timeout is set to the nearest deadline (idle connections and
// whatever the subclass reports), then due timers are processed after poll().
class EventServer {
public:
    explicit EventServer(uint16_t port) : port_(port) {}
    virtual ~EventServer();
    EventServer(const EventServer &) = delete;
    EventServer &operator=(const EventServer &) = delete;

    bool listen_now(std::string *err);
    void run();  // returns after request_stop() (e.g. from SIGINT/SIGTERM)
    static void request_stop();
    static void install_signal_handlers();

    void set_idle_timeout_ms(uint64_t ms) { idle_timeout_ms_ = ms; }
    size_t client_count() const { return num_clients_; }

protected:
    // Inbound: execute one request and write exactly one response value into `out`.
    virtual void handle_request(Conn *conn, std::vector<std::string> &cmd, Buffer &out) = 0;
    // Outbound connections.
    virtual void on_connected(Conn *) {}
    virtual void on_response(Conn *, const Value &) {}
    // Called for outbound conns before they are freed. Must not open new
    // connections directly; schedule a reconnect with a timer instead.
    virtual void on_closed(Conn *) {}
    // Subclass timers: absolute monotonic deadline of the nearest one (UINT64_MAX = none),
    // and the function that runs whatever is due.
    virtual uint64_t next_timer_ms() { return UINT64_MAX; }
    virtual void process_timers() {}

    Conn *connect_peer(const std::string &host, uint16_t port, std::string *err);
    void send_request(Conn *conn, const std::vector<std::string> &cmd);
    void set_idle_exempt(Conn *conn);

private:
    void accept_new();
    void handle_read(Conn *conn);
    void handle_write(Conn *conn);
    void finish_connect(Conn *conn);
    bool try_one_request(Conn *conn);
    bool try_one_response(Conn *conn);
    void destroy(Conn *conn);
    void destroy_closed();
    int poll_timeout();
    void process_idle();

    uint16_t port_;
    int listen_fd_ = -1;
    std::vector<Conn *> fd2conn_;  // all connections, indexed by fd
    DList idle_list_ = {&idle_list_, &idle_list_};  // dummy head, front = idle longest
    uint64_t idle_timeout_ms_ = 300 * 1000;
    size_t num_clients_ = 0;
};

}  // namespace distcache
