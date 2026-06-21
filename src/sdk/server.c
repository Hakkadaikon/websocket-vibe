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

// Try to complete the HTTP upgrade once \r\n\r\n is staged. Returns true when
// the 101 response has been sent (hs_done set), false if still incomplete, and
// sets *fail on a malformed request.
static bool try_handshake(client *c, bool *fail) {
    *fail = false;
    if (!(c->hs_len >= 4 && ws_memcmp(c->hs + c->hs_len - 4, "\r\n\r\n", 4) == 0))
        return false; // need more header bytes
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
    if (!write_all(c->fd, (const u8 *) resp, rlen)) {
        *fail = true;
        return false;
    }
    c->hs_done = true;
    ws_conn_init(&c->conn, WS_ROLE_SERVER, c->msg, MSGBUF);
    return true;
}

// Dispatch one drained event; returns false to terminate the connection.
static bool handle_event(client *c, const ws_event *ev) {
    size_t n = 0;
    switch (ev->type) {
    case WS_EV_MESSAGE:
        n = ws_send_message(&c->conn, ev->opcode, ev->data, ev->len, g_tx, TXBUF);
        return n && write_all(c->fd, g_tx, n);
    case WS_EV_PING:
        n = ws_send_pong(&c->conn, ev->data, ev->len, g_tx, TXBUF);
        return n && write_all(c->fd, g_tx, n);
    case WS_EV_CLOSE:
        n = ws_send_close(&c->conn, ev->close_code == 1005 ? 1000 : ev->close_code, g_tx, TXBUF);
        if (n)
            write_all(c->fd, g_tx, n);
        return false;
    case WS_EV_ERROR:
        n = ws_send_close(&c->conn, 1002, g_tx, TXBUF);
        if (n)
            write_all(c->fd, g_tx, n);
        return false;
    default:
        return true;
    }
}

// Feed a freshly read chunk through the connection; returns false to close.
static bool feed_frames(client *c, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t used = ws_conn_recv(&c->conn, buf + off, len - off);
        ws_event ev;
        while (ws_conn_poll(&c->conn, &ev) != WS_EV_NONE)
            if (!handle_event(c, &ev))
                return false;
        if (used == 0 && ws_conn_status(&c->conn) != WS_ST_OPEN)
            return false;
        off += used;
    }
    return true;
}

// Drain everything readable on one client until EAGAIN; returns false to close.
static bool service_client(client *c) {
    for (;;) {
        i64 n;
        if (!c->hs_done) {
            if (c->hs_len >= HSBUF)
                return false; // request too large
            n = sys_read(c->fd, c->hs + c->hs_len, HSBUF - c->hs_len);
        } else {
            n = sys_read(c->fd, g_rx, RXBUF);
        }
        if (n == 0)
            return false; // peer closed
        if (n < 0)
            return true; // EAGAIN/EWOULDBLOCK: nothing left for now, keep open
        if (!c->hs_done) {
            c->hs_len += (size_t) n;
            bool fail;
            if (!try_handshake(c, &fail))
                if (fail)
                    return false; // else: need more header bytes, loop reads again
        } else {
            if (!feed_frames(c, g_rx, (size_t) n))
                return false;
        }
    }
}

static int listen_socket(void) {
    int fd = sys_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (fd < 0)
        return fd;
    int one = 1;
    sys_setsockopt(fd, WS_SOL_SOCKET, WS_SO_REUSEADDR, &one, sizeof one);
    ws_sockaddr_in addr = {0};
    addr.sin_family = WS_AF_INET;
    addr.sin_port = ws_htons(PORT);
    addr.sin_addr = WS_INADDR_ANY;
    if (sys_bind(fd, &addr, sizeof addr) < 0)
        return -1;
    if (sys_listen(fd, 16) < 0)
        return -1;
    return fd;
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

// Accept every pending connection on the listening socket into a free slot.
static void accept_all(int epfd, int lfd) {
    for (;;) {
        int cfd = sys_accept(lfd, NULL, NULL);
        if (cfd < 0)
            break;
        client *c = slot_alloc(cfd);
        if (!c) {
            sys_close(cfd); // table full: refuse
            continue;
        }
        sys_set_nonblock(cfd);
        ep_add(epfd, cfd);
    }
}

// Handle one ready fd: accept on the listener, otherwise service the client.
static void on_ready(int epfd, int lfd, int fd) {
    if (fd == lfd) {
        accept_all(epfd, lfd);
        return;
    }
    client *c = slot_find(fd);
    if (c && !service_client(c))
        drop_client(epfd, c);
}

static int run(void) {
    for (int i = 0; i < MAX_CONN; i++)
        g_clients[i].fd = -1;
    int lfd = listen_socket();
    if (lfd < 0)
        return 1;
    sys_set_nonblock(lfd);
    int epfd = sys_epoll_create1(0);
    if (epfd < 0)
        return 1;
    ep_add(epfd, lfd);

    ws_epoll_event evs[MAX_CONN + 1];
    for (;;) {
        int nready = sys_epoll_wait(epfd, evs, MAX_CONN + 1, -1);
        for (int i = 0; i < nready; i++)
            on_ready(epfd, lfd, (int) evs[i].data);
    }
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
