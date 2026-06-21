// Behavioral spec for the sans-IO connection: framing, fragmentation,
// control-frame interleaving, masking rules, and protocol-violation detection
// (RFC6455 §5.4-5.6, §6). White-box include of the impl.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/frame.c"
#include "../../src/core/mask.c"
#include "../../src/core/utf8.c"
#include "../../src/platform/mem.c"
#include "../../src/platform/sys.c"
#include "../../src/sdk/conn.c"

// Build a client->server frame (masked) into buf. Returns total length.
static size_t mk_frame(u8 *buf, bool fin, u8 opcode, const u8 *payload, size_t plen) {
    u8 key[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    size_t n = ws_frame_build_header(buf, 14, fin, opcode, true, key, plen);
    for (size_t i = 0; i < plen; i++)
        buf[n + i] = payload[i];
    ws_mask(buf + n, plen, key);
    return n + plen;
}

static ws_conn C;
static u8 MSG[WS_MAX_MESSAGE];

static void reset(void) {
    assert(ws_conn_init(&C, WS_ROLE_SERVER, MSG, sizeof MSG));
}

static void feed_all(const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len)
        off += ws_conn_recv(&C, buf + off, len - off);
}

static void test_single_text(void) {
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "hello", 5);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.opcode == WS_OP_TEXT && ev.len == 5 && memcmp(ev.data, "hello", 5) == 0);
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE);
}

static void test_fragmented(void) {
    reset();
    u8 f[64];
    size_t n = mk_frame(f, false, WS_OP_TEXT, (const u8 *) "Hel", 3);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE); // not complete yet
    n = mk_frame(f, true, WS_OP_CONTINUATION, (const u8 *) "lo", 2);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 5 && memcmp(ev.data, "Hello", 5) == 0);
}

static void test_control_interleaved(void) {
    // A ping may arrive between fragments and must be surfaced separately
    // without corrupting the in-progress message (RFC6455 §5.4).
    reset();
    u8 f[64];
    size_t n = mk_frame(f, false, WS_OP_TEXT, (const u8 *) "ab", 2);
    feed_all(f, n);
    n = mk_frame(f, true, WS_OP_PING, (const u8 *) "pi", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PING);
    assert(ev.len == 2 && memcmp(ev.data, "pi", 2) == 0);
    n = mk_frame(f, true, WS_OP_CONTINUATION, (const u8 *) "cd", 2);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 4 && memcmp(ev.data, "abcd", 4) == 0);
}

static void test_reject_unmasked_client_frame(void) {
    // Server MUST reject an unmasked frame from the client (RFC6455 §5.1).
    reset();
    u8 f[16];
    size_t n = ws_frame_build_header(f, 16, true, WS_OP_TEXT, false, NULL, 1);
    f[n] = 'x';
    feed_all(f, n + 1);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_fragmented_control(void) {
    // Control frames MUST NOT be fragmented (RFC6455 §5.5).
    reset();
    u8 f[16];
    size_t n = mk_frame(f, false, WS_OP_PING, (const u8 *) "x", 1);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_oversize_control(void) {
    // Control frame payload > 125 is illegal (RFC6455 §5.5).
    reset();
    u8 big[126];
    memset(big, 'z', sizeof big);
    u8 f[140];
    size_t n = mk_frame(f, true, WS_OP_PING, big, 126);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_bad_utf8_text(void) {
    // Text frame with ill-formed UTF-8 is a protocol error (RFC6455 §8.1).
    reset();
    u8 bad[] = {0xC0, 0x80}; // overlong
    u8 f[16];
    size_t n = mk_frame(f, true, WS_OP_TEXT, bad, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_close_handshake(void) {
    reset();
    u8 payload[2] = {0x03, 0xE8}; // code 1000
    u8 f[16];
    size_t n = mk_frame(f, true, WS_OP_CLOSE, payload, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1000);
    assert(ws_conn_status(&C) == WS_ST_CLOSING || ws_conn_status(&C) == WS_ST_CLOSED);
}

static void test_send_message_unmasked_server(void) {
    reset();
    u8 out[64];
    size_t n = ws_send_message(&C, WS_OP_BINARY, (const u8 *) "hi", 2, out, sizeof out);
    assert(n == 4);               // 2 header + 2 payload, unmasked
    assert((out[1] & 0x80) == 0); // server frames not masked
    assert(out[0] == (0x80 | WS_OP_BINARY));
    assert(out[2] == 'h' && out[3] == 'i');
}

// --- bridge tests for the proven Spec.Workflow theorems ---

static void test_unsolicited_pong(void) {
    // C-06: an unsolicited pong is accepted; state unchanged, surfaced as PONG.
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_PONG, (const u8 *) "po", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PONG);
    assert(ev.len == 2 && memcmp(ev.data, "po", 2) == 0);
    assert(ws_conn_status(&C) == WS_ST_OPEN); // state preserved
}

static void test_pong_echoes_ping_payload(void) {
    // C-05: ping payload, fed back into ws_send_pong, yields a pong whose
    // payload matches the ping (pongFor / pong_echoes_ping_payload).
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_PING, (const u8 *) "data", 4);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PING && ev.len == 4);
    u8 out[64];
    size_t m = ws_send_pong(&C, ev.data, ev.len, out, sizeof out);
    // server pong: 2-byte header + 4 payload, unmasked, payload echoes ping.
    assert(m == 6 && (out[1] & 0x80) == 0);
    assert(memcmp(out + 2, "data", 4) == 0);
}

static void test_no_data_after_close_sent(void) {
    // S-03: after ws_send_close, the connection is no longer OPEN and data
    // frames are refused (ws_send_message returns 0).
    reset();
    u8 out[64];
    ws_send_close(&C, 1000, out, sizeof out);
    assert(ws_conn_status(&C) != WS_ST_OPEN);
    assert(ws_send_message(&C, WS_OP_TEXT, (const u8 *) "x", 1, out, sizeof out) == 0);
}

static void test_close_handshake_roundtrip(void) {
    // S-05: peer closes first (-> CLOSING); our reply completes the handshake
    // (-> CLOSED) and a second ws_send_close is a no-op (sent at most once).
    reset();
    u8 f[16];
    u8 body[2] = {0x03, 0xE8}; // 1000
    size_t n = mk_frame(f, true, WS_OP_CLOSE, body, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ws_conn_status(&C) == WS_ST_CLOSING); // peer closed, we have not replied
    u8 out[16];
    size_t r = ws_send_close(&C, ev.close_code, out, sizeof out);
    assert(r == 4);
    assert(ws_conn_status(&C) == WS_ST_CLOSED);            // handshake complete
    assert(ws_send_close(&C, 1000, out, sizeof out) == 0); // idempotent
}

static void test_data_discarded_after_close_recv(void) {
    // S-04: once a CLOSE is received (state CLOSING), a following data frame is
    // discarded — no MESSAGE event is produced.
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_CLOSE, (const u8 *) "\x03\xE8", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ws_conn_status(&C) == WS_ST_CLOSING);
    // Now a text frame arrives: it must NOT surface as a message.
    n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "late", 4);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE); // discarded
}

static void test_client_mask_key_random(void) {
    // RFC6455 §5.3: client frames are masked with a fresh, strong-RNG key.
    // Verify: frames are masked, unmask recovers the data, and two successive
    // frames do not reuse the same key (i.e. it is not the old fixed key).
    ws_conn cc;
    u8 buf[WS_MAX_MESSAGE];
    assert(ws_conn_init(&cc, WS_ROLE_CLIENT, buf, sizeof buf));
    u8 a[32], b[32];
    size_t na = ws_send_message(&cc, WS_OP_BINARY, (const u8 *) "payload!", 8, a, sizeof a);
    size_t nb = ws_send_message(&cc, WS_OP_BINARY, (const u8 *) "payload!", 8, b, sizeof b);
    assert(na == 14 && nb == 14);           // 2 hdr + 4 mask key + 8 payload
    assert((a[1] & 0x80) && (b[1] & 0x80)); // mask bit set
    // key is bytes [2..5]; with a strong RNG two frames almost surely differ.
    assert(memcmp(a + 2, b + 2, 4) != 0);
    // unmask recovers original payload.
    u8 key[4];
    memcpy(key, a + 2, 4);
    ws_mask(a + 6, 8, key);
    assert(memcmp(a + 6, "payload!", 8) == 0);
}

static void test_recv_invalid_close_code(void) {
    // M-07: receiving an out-of-range close code surfaces 1002 (Protocol Error).
    reset();
    u8 f[16];
    u8 bad[2] = {0x03, 0xED}; // 1005 — reserved, MUST NOT appear on the wire
    size_t n = mk_frame(f, true, WS_OP_CLOSE, bad, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1002);
}

static void test_recv_valid_close_code(void) {
    // M-07: a valid close code passes through unchanged.
    reset();
    u8 f[16];
    u8 ok[2] = {0x03, 0xE8}; // 1000 — valid
    size_t n = mk_frame(f, true, WS_OP_CLOSE, ok, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1000);
}

static void test_send_close_sanitizes_code(void) {
    // M-06: a reserved code (1006) must never be emitted; it is folded to 1000.
    reset();
    u8 out[16];
    size_t n = ws_send_close(&C, 1006, out, sizeof out);
    assert(n == 4); // 2 header + 2 payload
    u16 emitted = (u16) ((out[2] << 8) | out[3]);
    assert(emitted == 1000);
}

static void test_split_across_recv(void) {
    // Header and payload arriving in separate recv calls must aggregate.
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "split", 5);
    for (size_t k = 0; k < n; k++)
        ws_conn_recv(&C, f + k, 1); // one byte at a time
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 5 && memcmp(ev.data, "split", 5) == 0);
}

int main(void) {
    test_single_text();
    test_fragmented();
    test_control_interleaved();
    test_reject_unmasked_client_frame();
    test_reject_fragmented_control();
    test_reject_oversize_control();
    test_reject_bad_utf8_text();
    test_close_handshake();
    test_send_message_unmasked_server();
    test_unsolicited_pong();
    test_pong_echoes_ping_payload();
    test_no_data_after_close_sent();
    test_close_handshake_roundtrip();
    test_data_discarded_after_close_recv();
    test_client_mask_key_random();
    test_recv_invalid_close_code();
    test_recv_valid_close_code();
    test_send_close_sanitizes_code();
    test_split_across_recv();
    printf("test_conn: all passed\n");
    return 0;
}
