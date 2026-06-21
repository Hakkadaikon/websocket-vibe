// RFC6455 §4 opening handshake. No libc.
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

    u8 digest[20];
    ws_sha1(buf, n, digest);
    ws_base64_encode(digest, 20, out);
}

static char lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char) ((unsigned char) c | 0x20u); // ASCII to-lower, no narrowing
    return c;
}

// Case-insensitive compare of `req[at..]` against header name `name` (NUL-term).
// Returns name length on match, else 0.
static size_t match_name(const char *req, size_t len, size_t at, const char *name) {
    size_t i = 0;
    for (; name[i]; i++) {
        if (at + i >= len || lower(req[at + i]) != lower(name[i]))
            return 0;
    }
    return i;
}

// Length of the header value at `start`, trimming leading WS and stopping at \r or \n.
static size_t value_span(const char *req, size_t len, size_t *start) {
    size_t s = *start;
    while (s < len && (req[s] == ' ' || req[s] == '\t'))
        s++;
    size_t e = s;
    while (e < len && req[e] != '\r' && req[e] != '\n')
        e++;
    *start = s;
    return e - s;
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
            return cap + 1; // overflow sentinel
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
        return 0; // need room for the NUL too
    out[o] = 0;
    return o;
}
