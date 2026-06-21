#include "core/utf8.h"

// Lead-byte dispatch matching the proven branch boundaries
// (WsProof.Utf8): sets sequence length and the allowed range for the FIRST
// continuation byte (subsequent ones are always 0x80..0xBF).
// Returns false if `b` is not a valid lead/ASCII byte.
static bool classify_lead(ws_utf8_state *s, u8 b) {
    if (b <= 0x7F) {
        s->remaining = 0;
        return true;
    }
    if (b >= 0xC2 && b <= 0xDF) {
        s->remaining = 1;
        s->lo = 0x80;
        s->hi = 0xBF;
        return true;
    }
    if (b >= 0xE0 && b <= 0xEF) {
        s->remaining = 2;
        s->lo = (b == 0xE0) ? 0xA0 : 0x80;
        s->hi = (b == 0xED) ? 0x9F : 0xBF;
        return true;
    }
    if (b >= 0xF0 && b <= 0xF4) {
        s->remaining = 3;
        s->lo = (b == 0xF0) ? 0x90 : 0x80;
        s->hi = (b == 0xF4) ? 0x8F : 0xBF;
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
