// ws — minimal freestanding echo server demonstrating the SDK end to end.
// No libc: own _start, syscalls only. epoll-multiplexed: many clients at once.
// Echoes data messages, replies to ping with pong, completes the close handshake.
#include "ws/ws.h"

#include "platform/sys.h"

#include "core/frame.h"
#include "core/mask.h"
#include "core/utf8.h"
#include "platform/mem.h"
#include "protocol/handshake.h"

#define PORT     9001
#define MAX_CONN 64
#define RXBUF    (1u << 16)
// Demo aggregation cap per connection. Smaller than the SDK's 1 MiB default so
// MAX_CONN slots stay ~8 MiB of .bss, not ~64 MiB (still fits the E2E payloads).
#define MSGBUF (128u << 10)
#define TXBUF  (MSGBUF + 16)
#define HSBUF  2048 // upgrade request staging

// Per-connection state. The aggregation buffer must be per-connection so
// interleaved clients do not clobber each other's in-progress messages.
typedef struct {
    int fd; // -1 = slot free
    bool hs_done;
    size_t hs_len;
    u8 hs[HSBUF];
    ws_conn conn;
    u8 msg[MSGBUF];
} client;

static client g_clients[MAX_CONN];
static u8 g_rx[RXBUF]; // shared scratch: one connection processed at a time
static u8 g_tx[TXBUF];

static client *slot_alloc(int fd) {
    for (int i = 0; i < MAX_CONN; i++) {
        if (g_clients[i].fd < 0) {
            client *c = &g_clients[i];
            ws_memset(c, 0, sizeof *c);
            c->fd = fd;
            return c;
        }
    }
    return NULL; // table full
}

static client *slot_find(int fd) {
    for (int i = 0; i < MAX_CONN; i++)
        if (g_clients[i].fd == fd)
            return &g_clients[i];
    return NULL;
}

static bool write_all(int fd, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        i64 n = sys_write(fd, buf + off, len - off);
        if (n <= 0)
            return false;
        off += (size_t) n;
    }
    return true;
}

static bool hs_complete(const client *c) {
    return c->hs_len >= 4 && ws_memcmp(c->hs + c->hs_len - 4, "\r\n\r\n", 4) == 0;
}

// Build and send the 101 response for the staged request. Returns true on
// success; sets *fail on a malformed request or a failed write.
static bool hs_respond(client *c, bool *fail) {
    const char *key;
    size_t klen = ws_handshake_find_key((const char *) c->hs, c->hs_len, &key);
    if (klen == 0) {
        *fail = true;
        return false;
    }
    char accept[WS_ACCEPT_KEY_LEN + 1];
    ws_handshake_accept(key, klen, accept);
    char resp[256];
    size_t rlen = ws_handshake_response(accept, resp, sizeof resp);
    *fail = !write_all(c->fd, (const u8 *) resp, rlen);
    return !*fail;
}

// Try to complete the HTTP upgrade once \r\n\r\n is staged. Returns true when
// the 101 response has been sent (hs_done set), false if still incomplete, and
// sets *fail on a malformed request.
static bool try_handshake(client *c, bool *fail) {
    *fail = false;
    if (!hs_complete(c))
        return false; // need more header bytes
    if (!hs_respond(c, fail))
        return false;
    c->hs_done = true;
    ws_conn_init(&c->conn, WS_ROLE_SERVER, c->msg, MSGBUF);
    return true;
}

// Send n staged bytes (if any) and keep the connection open.
static bool reply_open(client *c, size_t n) {
    return n && write_all(c->fd, g_tx, n);
}

// Send n staged bytes (if any) and close the connection.
static bool reply_close(client *c, size_t n) {
    if (n)
        write_all(c->fd, g_tx, n);
    return false;
}

// Data-bearing events (message/ping): echo or pong, keep open. *handled tells
// whether ev->type was one of these.
static bool handle_data_event(client *c, const ws_event *ev, bool *handled) {
    *handled = true;
    if (ev->type == WS_EV_MESSAGE)
        return reply_open(c, ws_send_message(&c->conn, ev->opcode, ev->data, ev->len, g_tx, TXBUF));
    if (ev->type == WS_EV_PING)
        return reply_open(c, ws_send_pong(&c->conn, ev->data, ev->len, g_tx, TXBUF));
    *handled = false;
    return true;
}

// Echo the peer's close code, mapping the "no code present" sentinel to 1000.
static u16 echo_close_code(u16 code) {
    return code == 1005 ? 1000 : code;
}

// Terminating events (close/error): send the matching close, then close.
static bool handle_close_event(client *c, const ws_event *ev) {
    if (ev->type == WS_EV_ERROR)
        return reply_close(c, ws_send_close(&c->conn, 1002, g_tx, TXBUF));
    if (ev->type == WS_EV_CLOSE)
        return reply_close(c,
                           ws_send_close(&c->conn, echo_close_code(ev->close_code), g_tx, TXBUF));
    return true;
}

// Dispatch one drained event; returns false to terminate the connection.
static bool handle_event(client *c, const ws_event *ev) {
    bool handled;
    bool keep = handle_data_event(c, ev, &handled);
    return handled ? keep : handle_close_event(c, ev);
}

// Drain and dispatch all events currently buffered in the connection;
// returns false to close.
static bool drain_events(client *c) {
    ws_event ev;
    while (ws_conn_poll(&c->conn, &ev) != WS_EV_NONE)
        if (!handle_event(c, &ev))
            return false;
    return true;
}

// True when the connection made no progress and is no longer open (stuck).
static bool feed_stalled(const client *c, size_t used) {
    return used == 0 && ws_conn_status(&c->conn) != WS_ST_OPEN;
}

// Consume one slice starting at *off: dispatch its events and advance *off.
// Returns false to close the connection.
static bool feed_step(client *c, const u8 *buf, size_t len, size_t *off) {
    size_t used = ws_conn_recv(&c->conn, buf + *off, len - *off);
    if (!drain_events(c) || feed_stalled(c, used))
        return false;
    *off += used;
    return true;
}

// Feed a freshly read chunk through the connection; returns false to close.
static bool feed_frames(client *c, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len)
        if (!feed_step(c, buf, len, &off))
            return false;
    return true;
}

// Step outcome for one read+process iteration.
typedef enum { STEP_MORE, STEP_KEEP, STEP_DROP } step;

// Advance the handshake by the n bytes just read into c->hs.
static step hs_step(client *c, size_t n) {
    c->hs_len += n;
    bool fail = false;
    try_handshake(c, &fail); // incomplete header just loops for more bytes
    return fail ? STEP_DROP : STEP_MORE;
}

// Read once in the active phase. Returns the byte count or <=0 from sys_read;
// STEP-free, the caller classifies n.
static i64 service_read(client *c) {
    if (c->hs_done)
        return sys_read(c->fd, g_rx, RXBUF);
    if (c->hs_len >= HSBUF)
        return 0; // request too large: treat as peer-closed -> drop
    return sys_read(c->fd, c->hs + c->hs_len, HSBUF - c->hs_len);
}

// Process n>0 bytes just read in the active phase.
static step service_consume(client *c, size_t n) {
    if (!c->hs_done)
        return hs_step(c, n);
    return feed_frames(c, g_rx, n) ? STEP_MORE : STEP_DROP;
}

// Read once in the right phase and process the result.
static step service_step(client *c) {
    i64 n = service_read(c);
    if (n == 0)
        return STEP_DROP; // peer closed (or request too large)
    if (n < 0)
        return STEP_KEEP; // EAGAIN: nothing left for now, keep open
    return service_consume(c, (size_t) n);
}

// Drain everything readable on one client until EAGAIN; returns false to close.
static bool service_client(client *c) {
    for (;;) {
        step s = service_step(c);
        if (s == STEP_MORE)
            continue;
        return s == STEP_KEEP;
    }
}

// Bind fd to PORT and start listening. Returns 0 on success, <0 on failure.
static int bind_listen(int fd) {
    int one = 1;
    sys_setsockopt(fd, WS_SOL_SOCKET, WS_SO_REUSEADDR, &one, sizeof one);
    ws_sockaddr_in addr = {0};
    addr.sin_family = WS_AF_INET;
    addr.sin_port = ws_htons(PORT);
    addr.sin_addr = WS_INADDR_ANY;
    if (sys_bind(fd, &addr, sizeof addr) < 0)
        return -1;
    return sys_listen(fd, 16) < 0 ? -1 : 0;
}

static int listen_socket(void) {
    int fd = sys_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (fd < 0)
        return fd;
    return bind_listen(fd) < 0 ? -1 : fd;
}

static void ep_add(int epfd, int fd) {
    ws_epoll_event ev = {.events = WS_EPOLLIN, .data = (u64) fd};
    sys_epoll_ctl(epfd, WS_EPOLL_CTL_ADD, fd, &ev);
}

static void drop_client(int epfd, client *c) {
    sys_epoll_ctl(epfd, WS_EPOLL_CTL_DEL, c->fd, NULL);
    sys_close(c->fd);
    c->fd = -1;
}

// Register an accepted fd into a free slot, or refuse it if the table is full.
static void admit_client(int epfd, int cfd) {
    if (!slot_alloc(cfd)) {
        sys_close(cfd); // table full: refuse
        return;
    }
    sys_set_nonblock(cfd);
    ep_add(epfd, cfd);
}

// Accept every pending connection on the listening socket into a free slot.
static void accept_all(int epfd, int lfd) {
    for (;;) {
        int cfd = sys_accept(lfd, NULL, NULL);
        if (cfd < 0)
            break;
        admit_client(epfd, cfd);
    }
}

// Service a known client fd, dropping it if the connection should close.
static void on_client(int epfd, int fd) {
    client *c = slot_find(fd);
    if (c && !service_client(c))
        drop_client(epfd, c);
}

// Handle one ready fd: accept on the listener, otherwise service the client.
static void on_ready(int epfd, int lfd, int fd) {
    if (fd == lfd)
        accept_all(epfd, lfd);
    else
        on_client(epfd, fd);
}

static void clients_init(void) {
    for (int i = 0; i < MAX_CONN; i++)
        g_clients[i].fd = -1;
}

// Create the listener + epoll, registering the listener. Returns the epoll fd
// in *epfd and the listen fd in *lfd, or <0 on failure.
static int setup(int *epfd, int *lfd) {
    clients_init();
    *lfd = listen_socket();
    if (*lfd < 0)
        return -1;
    sys_set_nonblock(*lfd);
    *epfd = sys_epoll_create1(0);
    return *epfd < 0 ? -1 : (ep_add(*epfd, *lfd), 0);
}

// Block on epoll and dispatch every ready fd. Never returns.
static void event_loop(int epfd, int lfd) {
    ws_epoll_event evs[MAX_CONN + 1];
    for (;;) {
        int nready = sys_epoll_wait(epfd, evs, MAX_CONN + 1, -1);
        for (int i = 0; i < nready; i++)
            on_ready(epfd, lfd, (int) evs[i].data);
    }
}

static int run(void) {
    int epfd, lfd;
    if (setup(&epfd, &lfd) < 0)
        return 1;
    event_loop(epfd, lfd);
    return 0; // unreachable: event_loop never returns
}

// Freestanding entry point. The kernel jumps here with RSP 16-byte aligned,
// but the SysV ABI expects RSP%16==8 on function entry (as if after a CALL).
// Align then call so SSE moves (movaps) in callees don't fault.
// _start is the mandated process entry symbol; the reserved name is required.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c)
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %rbp, %rbp\n\t" // mark outermost frame
                     "and $-16, %rsp\n\t" // 16-byte align
                     "call ws_main\n\t"); // RSP%16==8 on entry, ABI-correct
}

// Real entry, called with a correctly aligned stack.
__attribute__((used, noreturn)) void ws_main(void) {
    sys_exit(run());
}
