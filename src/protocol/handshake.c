// RFC6455 §4 オープニングハンドシェイク。libc に依存しない。
#include "protocol/handshake.h"

#include "protocol/base64.h"
#include "protocol/sha1.h"

static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

void ws_handshake_accept(const char *key, size_t key_len, char out[WS_ACCEPT_KEY_LEN + 1]) {
    u8 buf[128];
    size_t n = 0;
    for (size_t i = 0; i < key_len; i++)
        buf[n++] = (u8) key[i];
    for (size_t i = 0; GUID[i]; i++)
        buf[n++] = (u8) GUID[i];

    u8 digest[WS_SHA1_DIGEST_LEN];
    ws_sha1(buf, n, digest);
    ws_base64_encode(digest, WS_SHA1_DIGEST_LEN, out);
}

static char lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char) ((unsigned char) c | 0x20u); // ASCII 小文字化。縮小変換を避けるためのキャスト
    return c;
}

// `req[at..]` (len 内) がヘッダ名の文字 `name[i]` と大文字小文字を無視して一致すれば真。
static int name_char_eq(const char *req, size_t len, size_t at, const char *name, size_t i) {
    return at + i < len && lower(req[at + i]) == lower(name[i]);
}

// `req[at..]` をヘッダ名 `name` (NUL 終端) と大文字小文字を無視して比較する。
// 一致すればヘッダ名の長さを、そうでなければ 0 を返す。
static size_t match_name(const char *req, size_t len, size_t at, const char *name) {
    size_t i = 0;
    for (; name[i]; i++) {
        if (!name_char_eq(req, len, at, name, i))
            return 0;
    }
    return i;
}

static int is_hws(char c) {
    return c == ' ' || c == '\t';
}

static int is_eol(char c) {
    return c == '\r' || c == '\n';
}

// `s` から始まる先頭の水平方向の空白を読み飛ばす (len で上限を制限)。
static size_t skip_hws(const char *req, size_t len, size_t s) {
    while (s < len && is_hws(req[s]))
        s++;
    return s;
}

// `s` から最初の行末文字まで進める (len で上限を制限)。
static size_t to_eol(const char *req, size_t len, size_t s) {
    while (s < len && !is_eol(req[s]))
        s++;
    return s;
}

// `start` のヘッダ値の長さ。先頭の空白を除き、\r または \n で止まる。
static size_t value_span(const char *req, size_t len, size_t *start) {
    size_t s = skip_hws(req, len, *start);
    *start = s;
    return to_eol(req, len, s) - s;
}

size_t ws_handshake_find_key(const char *req, size_t len, const char **val) {
    static const char NAME[] = "sec-websocket-key:";
    for (size_t i = 0; i < len; i++) {
        size_t nl = match_name(req, len, i, NAME);
        if (nl == 0)
            continue;
        size_t s = i + nl;
        size_t n = value_span(req, len, &s);
        *val = req + s;
        return n;
    }
    return 0;
}

static size_t append(char *out, size_t o, size_t cap, const char *s) {
    for (size_t i = 0; s[i]; i++) {
        if (o >= cap)
            return cap + 1; // オーバーフローを示すセンチネル値
        out[o++] = s[i];
    }
    return o;
}

size_t ws_handshake_response(const char *accept, char *out, size_t cap) {
    size_t o = 0;
    o = append(out, o, cap, "HTTP/1.1 101 Switching Protocols\r\n");
    o = append(out, o, cap, "Upgrade: websocket\r\n");
    o = append(out, o, cap, "Connection: Upgrade\r\n");
    o = append(out, o, cap, "Sec-WebSocket-Accept: ");
    o = append(out, o, cap, accept);
    o = append(out, o, cap, "\r\n\r\n");
    if (o >= cap)
        return 0; // NUL を入れる領域も必要
    out[o] = 0;
    return o;
}
