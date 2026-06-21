// Echo の例 — テキストメッセージをそのまま返す素の libc WebSocket サーバ。
// src/sdk/server.c の freestanding デモと違い、通常のブロッキングソケットから
// sans-IO コア(ws.h)を駆動する方法を示す。同時接続は 1 つだけ。
#include "ws/ws.h"

#include "protocol/handshake.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PORT  9002
#define RXBUF (1u << 16)

static int read_handshake(int fd, char *buf, size_t cap) {
    size_t got = 0;
    while (got < cap) {
        ssize_t n = read(fd, buf + got, cap - got);
        if (n <= 0)
            return -1;
        got += (size_t) n;
        if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
            return (int) got;
    }
    return -1; // リクエストがバッファより大きい
}

// RFC6455 §4: アップグレード要求を読み、算出した accept キー付きで 101 を返す。
static bool do_handshake(int fd) {
    char req[RXBUF];
    int len = read_handshake(fd, req, sizeof req);
    if (len < 0)
        return false;
    const char *key;
    size_t klen = ws_handshake_find_key(req, (size_t) len, &key);
    if (klen == 0)
        return false;
    char accept[WS_ACCEPT_KEY_LEN + 1];
    ws_handshake_accept(key, klen, accept);
    char resp[256];
    size_t rlen = ws_handshake_response(accept, resp, sizeof resp);
    return write(fd, resp, rlen) == (ssize_t) rlen;
}

static bool write_all(int fd, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0)
            return false;
        off += (size_t) n;
    }
    return true;
}

// 1 回の recv() で得たイベントを処理しきって反応する。false を返すと接続を閉じる。
static bool handle_events(int fd, ws_conn *c, u8 *tx, size_t tx_cap) {
    ws_event ev;
    while (ws_conn_poll(c, &ev) != WS_EV_NONE) {
        size_t n = 0;
        switch (ev.type) {
        case WS_EV_MESSAGE: // メッセージをそのまま返送する
            n = ws_send_message(c, ev.opcode, ev.data, ev.len, tx, tx_cap);
            if (!n || !write_all(fd, tx, n))
                return false;
            break;
        case WS_EV_PING:
            n = ws_send_pong(c, ev.data, ev.len, tx, tx_cap);
            if (!n || !write_all(fd, tx, n))
                return false;
            break;
        case WS_EV_CLOSE:
            n = ws_send_close(c, ev.close_code == 1005 ? 1000 : ev.close_code, tx, tx_cap);
            if (n)
                write_all(fd, tx, n);
            return false;
        case WS_EV_ERROR:
            n = ws_send_close(c, 1002, tx, tx_cap);
            if (n)
                write_all(fd, tx, n);
            return false;
        default:
            break;
        }
    }
    return true;
}

static void serve(int fd) {
    if (!do_handshake(fd))
        return;
    static u8 msg[WS_MAX_MESSAGE];
    static u8 tx[WS_MAX_MESSAGE + 16];
    ws_conn c;
    ws_conn_init(&c, WS_ROLE_SERVER, msg, sizeof msg);
    u8 rx[RXBUF];
    for (;;) {
        ssize_t r = read(fd, rx, sizeof rx);
        if (r <= 0)
            return;
        size_t off = 0;
        while (off < (size_t) r) {
            size_t used = ws_conn_recv(&c, rx + off, (size_t) r - off);
            if (!handle_events(fd, &c, tx, sizeof tx))
                return;
            if (used == 0 && ws_conn_status(&c) != WS_ST_OPEN)
                return;
            off += used;
        }
    }
}

int main(void) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
        return 1;
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = INADDR_ANY};
    if (bind(lfd, (struct sockaddr *) &addr, sizeof addr) < 0 || listen(lfd, 16) < 0)
        return 1;
    printf("echo server listening on ws://127.0.0.1:%d\n", PORT);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0)
            continue;
        serve(cfd);
        close(cfd);
    }
}
