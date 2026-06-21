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

// 2-byte extended length (len7 == 126). Enforces minimal encoding.
static ws_parse_status parse_len16(const u8 *buf, size_t len, u64 *out_len, size_t *extra) {
    if (len < 4)
        return WS_PARSE_NEED_MORE;
    u64 v = read_be(buf + 2, 2);
    if (v < 126) // non-minimal: should have used 1-byte form
        return WS_PARSE_ERROR;
    *out_len = v;
    *extra = 2;
    return WS_PARSE_OK;
}

// Validate a decoded 8-byte length: high bit clear (RFC6455 §5.2) and minimal
// (must have used the 2-byte form for values that fit).
static ws_parse_status check_len64(u64 v) {
    if (v & 0x8000000000000000u) // high bit must be 0 (RFC6455 §5.2)
        return WS_PARSE_ERROR;
    if (v <= 0xFFFF) // non-minimal: should have used 2-byte form
        return WS_PARSE_ERROR;
    return WS_PARSE_OK;
}

// 8-byte extended length (len7 == 127). Enforces minimal encoding and the
// reserved high bit (RFC6455 §5.2).
static ws_parse_status parse_len64(const u8 *buf, size_t len, u64 *out_len, size_t *extra) {
    if (len < 10)
        return WS_PARSE_NEED_MORE;
    u64 v = read_be(buf + 2, 8);
    ws_parse_status st = check_len64(v);
    if (st != WS_PARSE_OK)
        return st;
    *out_len = v;
    *extra = 8;
    return WS_PARSE_OK;
}

// Decode the length field; sets *extra to extra bytes after the 2nd byte.
// Returns parse status; enforces minimal (canonical) encoding.
static ws_parse_status parse_len(const u8 *buf, size_t len, u8 len7, u64 *out_len, size_t *extra) {
    if (len7 < 126) {
        *out_len = len7;
        *extra = 0;
        return WS_PARSE_OK;
    }
    if (len7 == 126)
        return parse_len16(buf, len, out_len, extra);
    return parse_len64(buf, len, out_len, extra);
}

// Fill the 4-byte mask key from src, or zero it when src is null.
static void fill_mask_key(u8 dst[4], const u8 *src) {
    for (size_t i = 0; i < 4; i++)
        dst[i] = src ? src[i] : 0;
}

// Header bytes contributed by the masking key (4 if masked, else 0).
static size_t mask_len(bool masked) {
    return masked ? 4u : 0u;
}

// Unpack the first header byte (FIN/RSV/opcode) into out.
static void unpack_b0(ws_frame_header *out, u8 b0) {
    out->fin = (b0 & 0x80u) != 0;
    out->rsv1 = (b0 & 0x40u) != 0;
    out->rsv2 = (b0 & 0x20u) != 0;
    out->rsv3 = (b0 & 0x10u) != 0;
    out->opcode = b0 & 0x0Fu;
}

// Fill out from a validated header: b0 flags, mask key, length and totals.
// `extra` is the length-field byte count after byte 1; `plen` the payload len.
static void fill_header(ws_frame_header *out, const u8 *buf, bool masked, size_t extra, u64 plen) {
    const u8 *key = masked ? buf + 2 + extra : NULL;
    unpack_b0(out, buf[0]);
    out->masked = masked;
    out->payload_len = plen;
    out->header_len = 2 + extra + mask_len(masked);
    fill_mask_key(out->mask_key, key);
}

// Decode length and verify the full header (incl. mask key) is buffered.
// On OK, *plen/*extra are set and *masked reflects the mask flag.
static ws_parse_status parse_meta(const u8 *buf, size_t len, bool *masked, u64 *plen,
                                  size_t *extra) {
    ws_parse_status st = parse_len(buf, len, buf[1] & 0x7Fu, plen, extra);
    if (st != WS_PARSE_OK)
        return st;
    *masked = (buf[1] & 0x80u) != 0;
    if (len < 2 + *extra + mask_len(*masked))
        return WS_PARSE_NEED_MORE;
    return WS_PARSE_OK;
}

ws_parse_status ws_frame_parse_header(const u8 *buf, size_t len, ws_frame_header *out) {
    if (len < 2)
        return WS_PARSE_NEED_MORE;

    bool masked;
    u64 plen;
    size_t extra;
    ws_parse_status st = parse_meta(buf, len, &masked, &plen, &extra);
    if (st != WS_PARSE_OK)
        return st;

    fill_header(out, buf, masked, extra, plen);
    return WS_PARSE_OK;
}

// --- build ---

// Extra length bytes (beyond the first 2 header bytes) for a payload length.
static size_t len_extra(u64 payload_len) {
    if (payload_len <= 125)
        return 0;
    if (payload_len <= 0xFFFF)
        return 2;
    return 8;
}

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

// Append the masking flag and key at index n; returns new header length.
static size_t append_mask(u8 *buf, size_t n, const u8 mask_key[4]) {
    buf[1] |= 0x80u;
    for (size_t i = 0; i < 4; i++)
        buf[n + i] = mask_key[i];
    return n + 4;
}

// First header byte: FIN flag plus the 4-bit opcode.
static u8 pack_b0(bool fin, u8 opcode) {
    return (u8) ((fin ? 0x80u : 0u) | (opcode & 0x0Fu));
}

size_t ws_frame_build_header(u8 *buf, size_t cap, bool fin, u8 opcode, bool masked,
                             const u8 mask_key[4], u64 payload_len) {
    size_t need = 2 + len_extra(payload_len) + mask_len(masked);
    if (cap < need)
        return 0;

    buf[0] = pack_b0(fin, opcode);
    size_t n = build_len(buf, payload_len);
    return masked ? append_mask(buf, n, mask_key) : n;
}
