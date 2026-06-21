#include "core/utf8.h"

// Lead-byte dispatch matching the proven branch boundaries
// (WsProof.Utf8): sets sequence length and the allowed range for the FIRST
// continuation byte (subsequent ones are always 0x80..0xBF).
// Returns false if `b` is not a valid lead/ASCII byte.
static void set_seq(ws_utf8_state *s, u8 remaining, u8 lo, u8 hi) {
    s->remaining = remaining;
    s->lo        = lo;
    s->hi        = hi;
}

// 3-byte leads: E0 forbids overlong (1st cont >= A0), ED forbids surrogates
// (1st cont <= 9F), otherwise the generic 80..BF range.
static void lead_3byte(ws_utf8_state *s, u8 b) {
    u8 lo = (b == 0xE0) ? 0xA0 : 0x80;
    u8 hi = (b == 0xED) ? 0x9F : 0xBF;
    set_seq(s, 2, lo, hi);
}

// 4-byte leads: F0 forbids overlong (1st cont >= 90), F4 caps at U+10FFFF
// (1st cont <= 8F), otherwise generic.
static void lead_4byte(ws_utf8_state *s, u8 b) {
    u8 lo = (b == 0xF0) ? 0x90 : 0x80;
    u8 hi = (b == 0xF4) ? 0x8F : 0xBF;
    set_seq(s, 3, lo, hi);
}

static bool classify_lead(ws_utf8_state *s, u8 b) {
    if (b <= 0x7F) {
        s->remaining = 0;
        return true;
    }
    if (b >= 0xC2 && b <= 0xDF) {
        set_seq(s, 1, 0x80, 0xBF);
        return true;
    }
    if (b >= 0xE0 && b <= 0xEF) {
        lead_3byte(s, b);
        return true;
    }
    if (b >= 0xF0 && b <= 0xF4) {
        lead_4byte(s, b);
        return true;
    }
    return false; // 0x80..0xC1 (incl. overlong C0/C1) and 0xF5..0xFF
}

static bool feed_byte(ws_utf8_state *s, u8 b) {
    if (s->remaining == 0)
        return classify_lead(s, b);
    // Continuation byte: must lie in the currently allowed range.
    if (b < s->lo || b > s->hi)
        return false;
    s->remaining--;
    // After the first continuation, the range relaxes to the generic one.
    s->lo = 0x80;
    s->hi = 0xBF;
    return true;
}

bool ws_utf8_feed(ws_utf8_state *s, const u8 *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!feed_byte(s, buf[i]))
            return false;
    }
    return true;
}

bool ws_utf8_complete(const ws_utf8_state *s) {
    return s->remaining == 0;
}

bool ws_utf8_valid(const u8 *buf, size_t len) {
    ws_utf8_state s = {0};
    return ws_utf8_feed(&s, buf, len) && ws_utf8_complete(&s);
}
