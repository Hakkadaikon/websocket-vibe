#include "core/frame.h"

// --- big-endian helpers (proven model: fromBE / byteAt) ---

static u64 read_be(const u8 *p, size_t n) {
    u64 v = 0;
    for (size_t i = 0; i < n; i++)
        v = (v << 8) | p[i];
    return v;
}

static void write_be(u8 *p, size_t n, u64 v) {
    for (size_t i = 0; i < n; i++)
        p[n - 1 - i] = (u8) (v >> (8 * i));
}

// --- parse ---

// Decode the length field; sets *consumed to extra bytes after the 2nd byte.
// Returns parse status; enforces minimal (canonical) encoding.
static ws_parse_status parse_len(const u8 *buf, size_t len, u8 len7, u64 *out_len, size_t *extra) {
    if (len7 < 126) {
        *out_len = len7;
        *extra = 0;
        return WS_PARSE_OK;
    }
    if (len7 == 126) {
        if (len < 4)
            return WS_PARSE_NEED_MORE;
        u64 v = read_be(buf + 2, 2);
        if (v < 126) // non-minimal: should have used 1-byte form
            return WS_PARSE_ERROR;
        *out_len = v;
        *extra = 2;
        return WS_PARSE_OK;
    }
    // len7 == 127
    if (len < 10)
        return WS_PARSE_NEED_MORE;
    if (buf[2] & 0x80u) // high bit must be 0 (RFC6455 §5.2)
        return WS_PARSE_ERROR;
    u64 v = read_be(buf + 2, 8);
    if (v <= 0xFFFF) // non-minimal: should have used 2-byte form
        return WS_PARSE_ERROR;
    *out_len = v;
    *extra = 8;
    return WS_PARSE_OK;
}

ws_parse_status ws_frame_parse_header(const u8 *buf, size_t len, ws_frame_header *out) {
    if (len < 2)
        return WS_PARSE_NEED_MORE;

    u8 b0 = buf[0], b1 = buf[1];
    u64 plen;
    size_t extra;
    ws_parse_status st = parse_len(buf, len, b1 & 0x7Fu, &plen, &extra);
    if (st != WS_PARSE_OK)
        return st;

    bool masked = (b1 & 0x80u) != 0;
    size_t hdr = 2 + extra + (masked ? 4u : 0u);
    if (len < hdr)
        return WS_PARSE_NEED_MORE;

    out->fin = (b0 & 0x80u) != 0;
    out->rsv1 = (b0 & 0x40u) != 0;
    out->rsv2 = (b0 & 0x20u) != 0;
    out->rsv3 = (b0 & 0x10u) != 0;
    out->opcode = b0 & 0x0Fu;
    out->masked = masked;
    out->payload_len = plen;
    out->header_len = hdr;
    if (masked) {
        for (size_t i = 0; i < 4; i++)
            out->mask_key[i] = buf[2 + extra + i];
    } else {
        for (size_t i = 0; i < 4; i++)
            out->mask_key[i] = 0;
    }
    return WS_PARSE_OK;
}

// --- build ---

// Emit length field into buf starting at index 1; returns total header bytes
// before the (optional) mask key. Always minimal form.
static size_t build_len(u8 *buf, u64 payload_len) {
    if (payload_len <= 125) {
        buf[1] = (u8) payload_len;
        return 2;
    }
    if (payload_len <= 0xFFFF) {
        buf[1] = 126;
        write_be(buf + 2, 2, payload_len);
        return 4;
    }
    buf[1] = 127;
    write_be(buf + 2, 8, payload_len);
    return 10;
}

size_t ws_frame_build_header(u8 *buf, size_t cap, bool fin, u8 opcode, bool masked,
                             const u8 mask_key[4], u64 payload_len) {
    size_t need = 2 +
                  (payload_len <= 125      ? 0u
                   : payload_len <= 0xFFFF ? 2u
                                           : 8u) +
                  (masked ? 4u : 0u);
    if (cap < need)
        return 0;

    buf[0] = (u8) ((fin ? 0x80u : 0u) | (opcode & 0x0Fu));
    size_t n = build_len(buf, payload_len);
    if (masked) {
        buf[1] |= 0x80u;
        for (size_t i = 0; i < 4; i++)
            buf[n + i] = mask_key[i];
        n += 4;
    }
    return n;
}
