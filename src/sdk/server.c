// ws — minimal freestanding echo server demonstrating the SDK end to end.
// No libc: own _start, syscalls only. Single-threaded, one client at a time.
// Echoes data messages, replies to ping with pong, honors close.
#include "ws/ws.h"

#include "platform/sys.h"

#include "core/frame.h"
#include "core/mask.h"
#include "core/utf8.h"
#include "platform/mem.h"
#include "protocol/handshake.h"

#define PORT     9001
#define RXBUF    (1u << 16)
#define MSGBUF   WS_MAX_MESSAGE
#define TXBUF    (WS_MAX_MESSAGE + 16)

static u8 g_rx[RXBUF];
static u8 g_msg[MSGBUF];
static u8 g_tx[TXBUF];

// Read the HTTP upgrade request (until \r\n\r\n), compute accept, send 101.
// Returns the listening client fd's first post-handshake byte offset already
// consumed (0; we keep it simple and assume no pipelined frame in the request).
static bool do_handshake(int cfd) {
    size_t got = 0;
    while (got < RXBUF) {
        i64 n = sys_read(cfd, g_rx + got, RXBUF - got);
        if (n <= 0)
            return false;
        got += (size_t) n;
        // crude end-of-headers check
        if (got >= 4 && ws_memcmp(g_rx + got - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    const char *key;
    size_t      klen = ws_handshake_find_key((const char *) g_rx, got, &key);
    if (klen == 0)
        return false;
    char accept[WS_ACCEPT_KEY_LEN + 1];
    ws_handshake_accept(key, klen, accept);
    char   resp[256];
    size_t rlen = ws_handshake_response(accept, resp, sizeof resp);
    return sys_write(cfd, resp, rlen) == (i64) rlen;
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

// Dispatch one drained event; returns false to terminate the connection.
static bool handle_event(int cfd, ws_conn *c, const ws_event *ev) {
    size_t n = 0;
    switch (ev->type) {
    case WS_EV_MESSAGE:
        n = ws_send_message(c, ev->opcode, ev->data, ev->len, g_tx, TXBUF);
        return n && write_all(cfd, g_tx, n);
    case WS_EV_PING:
        n = ws_send_pong(c, ev->data, ev->len, g_tx, TXBUF);
        return n && write_all(cfd, g_tx, n);
    case WS_EV_CLOSE:
        n = ws_send_close(c, ev->close_code == 1005 ? 1000 : ev->close_code, g_tx, TXBUF);
        if (n)
            write_all(cfd, g_tx, n);
        return false;
    case WS_EV_ERROR:
        n = ws_send_close(c, 1002, g_tx, TXBUF);
        if (n)
            write_all(cfd, g_tx, n);
        return false;
    default:
        return true;
    }
}

static void serve_client(int cfd) {
    if (!do_handshake(cfd)) {
        sys_close(cfd);
        return;
    }
    ws_conn conn;
    ws_conn_init(&conn, WS_ROLE_SERVER, g_msg, MSGBUF);
    for (;;) {
        i64 n = sys_read(cfd, g_rx, RXBUF);
        if (n <= 0)
            break;
        size_t off = 0;
        while (off < (size_t) n) {
            size_t used = ws_conn_recv(&conn, g_rx + off, (size_t) n - off);
            ws_event ev;
            while (ws_conn_poll(&conn, &ev) != WS_EV_NONE) {
                if (!handle_event(cfd, &conn, &ev)) {
                    sys_close(cfd);
                    return;
                }
            }
            if (used == 0 && ws_conn_status(&conn) != WS_ST_OPEN)
                break;
            off += used;
        }
    }
    sys_close(cfd);
}

static int listen_socket(void) {
    int fd = sys_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (fd < 0)
        return fd;
    int one = 1;
    sys_setsockopt(fd, WS_SOL_SOCKET, WS_SO_REUSEADDR, &one, sizeof one);
    ws_sockaddr_in addr = {0};
    addr.sin_family     = WS_AF_INET;
    addr.sin_port       = ws_htons(PORT);
    addr.sin_addr       = WS_INADDR_ANY;
    if (sys_bind(fd, &addr, sizeof addr) < 0)
        return -1;
    if (sys_listen(fd, 16) < 0)
        return -1;
    return fd;
}

static int run(void) {
    int lfd = listen_socket();
    if (lfd < 0)
        return 1;
    for (;;) {
        int cfd = sys_accept(lfd, NULL, NULL);
        if (cfd < 0)
            continue;
        serve_client(cfd);
    }
}

// Freestanding entry point. The kernel jumps here with RSP 16-byte aligned,
// but the SysV ABI expects RSP%16==8 on function entry (as if after a CALL).
// Align then call so SSE moves (movaps) in callees don't fault.
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %rbp, %rbp\n\t" // mark outermost frame
                     "and $-16, %rsp\n\t" // 16-byte align
                     "call ws_main\n\t"); // RSP%16==8 on entry, ABI-correct
}

// Real entry, called with a correctly aligned stack.
__attribute__((used, noreturn)) void ws_main(void) {
    sys_exit(run());
}
