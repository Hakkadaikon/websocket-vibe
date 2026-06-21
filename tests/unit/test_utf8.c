// White-box test for UTF-8 validator. Cases mirror the Lean WsProof.Utf8
// #eval checks (validate_correct). Plus the streaming/split-sequence path
// and the Autobahn 6.x fragmented-boundary cases.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/utf8.c"

#define VALID(...)                                                                                 \
    do {                                                                                           \
        u8 b[] = {__VA_ARGS__};                                                                    \
        assert(ws_utf8_valid(b, sizeof b) == true);                                                \
    } while (0)

#define INVALID(...)                                                                               \
    do {                                                                                           \
        u8 b[] = {__VA_ARGS__};                                                                    \
        assert(ws_utf8_valid(b, sizeof b) == false);                                               \
    } while (0)

static void test_valid(void) {
    assert(ws_utf8_valid((const u8 *) "", 0) == true); // empty
    VALID('h', 'e', 'l', 'l', 'o');                    // ASCII
    VALID(0xC3, 0xA9);                                 // é
    VALID(0xE2, 0x82, 0xAC);                           // €
    VALID(0xF0, 0x9F, 0x98, 0x80);                     // 😀
    VALID(0xED, 0x9F, 0xBF);                           // U+D7FF (just below surrogates)
    VALID(0xEE, 0x80, 0x80);                           // U+E000 (just above)
    VALID(0xF4, 0x8F, 0xBF, 0xBF);                     // U+10FFFF (max)
}

static void test_invalid(void) {
    INVALID(0x80);                   // lone continuation
    INVALID(0xC0, 0x80);             // overlong (2-byte)
    INVALID(0xC1, 0xBF);             // overlong (2-byte)
    INVALID(0xE0, 0x80, 0x80);       // overlong (3-byte)
    INVALID(0xED, 0xA0, 0x80);       // surrogate U+D800
    INVALID(0xED, 0xBF, 0xBF);       // surrogate U+DFFF
    INVALID(0xF0, 0x80, 0x80, 0x80); // overlong (4-byte)
    INVALID(0xF4, 0x90, 0x80, 0x80); // > U+10FFFF
    INVALID(0xF5, 0x80, 0x80, 0x80); // > U+10FFFF lead
    INVALID(0xE2, 0x82);             // truncated
    INVALID(0xFF);                   // invalid byte
    INVALID('a', 0xC3, 0x28);        // bad continuation
}

static void test_streaming_split(void) {
    // €  = E2 82 AC split across three feeds must still validate.
    ws_utf8_state s = {0};
    u8 a = 0xE2, b = 0x82, c = 0xAC;
    assert(ws_utf8_feed(&s, &a, 1) == true);
    assert(ws_utf8_complete(&s) == false); // pending mid-sequence
    assert(ws_utf8_feed(&s, &b, 1) == true);
    assert(ws_utf8_feed(&s, &c, 1) == true);
    assert(ws_utf8_complete(&s) == true);
}

static void test_streaming_bad_split(void) {
    ws_utf8_state s = {0};
    u8 lead = 0xE0, bad = 0x80; // E0 requires next in A0..BF; 80 is overlong
    assert(ws_utf8_feed(&s, &lead, 1) == true);
    assert(ws_utf8_feed(&s, &bad, 1) == false);
}

static void test_trailing_partial(void) {
    u8 lead = 0xE2;
    assert(ws_utf8_valid(&lead, 1) == false); // incomplete at end of buffer
}

int main(void) {
    test_valid();
    test_invalid();
    test_streaming_split();
    test_streaming_bad_split();
    test_trailing_partial();
    printf("test_utf8: all passed\n");
    return 0;
}
