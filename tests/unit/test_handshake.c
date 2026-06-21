// White-box test for handshake: SHA-1 + base64 + accept-key + header parse.
// Oracles: FIPS-180/RFC3174 SHA-1 vectors, RFC4648 base64 vectors, and the
// canonical RFC6455 §1.3 handshake example.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/protocol/base64.c"
#include "../../src/protocol/handshake.c"
#include "../../src/protocol/sha1.c"

static void hex(const u8 *d, size_t n, char *out) {
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = h[d[i] >> 4];
        out[2 * i + 1] = h[d[i] & 0xF];
    }
    out[2 * n] = 0;
}

static void test_sha1_vectors(void) {
    u8 d[20];
    char s[41];
    ws_sha1((const u8 *) "abc", 3, d);
    hex(d, 20, s);
    assert(strcmp(s, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);

    ws_sha1((const u8 *) "", 0, d);
    hex(d, 20, s);
    assert(strcmp(s, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);

    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ws_sha1((const u8 *) m, strlen(m), d);
    hex(d, 20, s);
    assert(strcmp(s, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);
}

static void test_base64_vectors(void) {
    char o[64];
    ws_base64_encode((const u8 *) "", 0, o);
    assert(strcmp(o, "") == 0);
    ws_base64_encode((const u8 *) "f", 1, o);
    assert(strcmp(o, "Zg==") == 0);
    ws_base64_encode((const u8 *) "fo", 2, o);
    assert(strcmp(o, "Zm8=") == 0);
    ws_base64_encode((const u8 *) "foo", 3, o);
    assert(strcmp(o, "Zm9v") == 0);
    ws_base64_encode((const u8 *) "foobar", 6, o);
    assert(strcmp(o, "Zm9vYmFy") == 0);
}

static void test_accept_key(void) {
    // RFC6455 §1.3 canonical example.
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    char acc[WS_ACCEPT_KEY_LEN + 1];
    ws_handshake_accept(key, strlen(key), acc);
    assert(strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
}

static void test_find_key(void) {
    const char *req = "GET /chat HTTP/1.1\r\n"
                      "Host: server.example.com\r\n"
                      "Upgrade: websocket\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n\r\n";
    const char *val = NULL;
    size_t n = ws_handshake_find_key(req, strlen(req), &val);
    assert(n == 24);
    assert(memcmp(val, "dGhlIHNhbXBsZSBub25jZQ==", 24) == 0);

    // Case-insensitive header name.
    const char *req2 = "sec-websocket-key:   abc\r\n\r\n";
    n = ws_handshake_find_key(req2, strlen(req2), &val);
    assert(n == 3 && memcmp(val, "abc", 3) == 0);

    // Absent.
    const char *req3 = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    assert(ws_handshake_find_key(req3, strlen(req3), &val) == 0);
}

static void test_response(void) {
    char out[256];
    size_t n = ws_handshake_response("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out, sizeof out);
    assert(n > 0);
    assert(strstr(out, "HTTP/1.1 101 Switching Protocols\r\n") == out);
    assert(strstr(out, "Upgrade: websocket\r\n") != NULL);
    assert(strstr(out, "Connection: Upgrade\r\n") != NULL);
    assert(strstr(out, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != NULL);
    assert(strstr(out, "\r\n\r\n") != NULL); // terminator present
}

int main(void) {
    test_sha1_vectors();
    test_base64_vectors();
    test_accept_key();
    test_find_key();
    test_response();
    printf("test_handshake: all passed\n");
    return 0;
}
