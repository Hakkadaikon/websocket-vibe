// White-box test for ws core masking. Predicates mirror the Lean proofs
// (WsProof.Masking): involution and length preservation.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/mask.c"

static void test_xor_spec(void) {
    // transformed[i] = data[i] ^ key[i % 4]
    u8 data[7] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    u8 key[4]  = {0xDE, 0xAD, 0xBE, 0xEF};
    u8 expect[7];
    for (size_t i = 0; i < 7; i++)
        expect[i] = (u8) (data[i] ^ key[i % 4]);
    ws_mask(data, 7, key);
    assert(memcmp(data, expect, 7) == 0);
}

static void test_involution(void) {
    u8 data[33];
    u8 orig[33];
    for (size_t i = 0; i < 33; i++)
        data[i] = orig[i] = (u8) (i * 7 + 1);
    u8 key[4] = {0x01, 0x80, 0xFF, 0x7A};
    ws_mask(data, 33, key);
    // After one mask, must differ somewhere (key not all-zero).
    assert(memcmp(data, orig, 33) != 0);
    ws_mask(data, 33, key); // mask again == unmask
    assert(memcmp(data, orig, 33) == 0);
}

static void test_zero_len(void) {
    u8 key[4] = {1, 2, 3, 4};
    ws_mask(NULL, 0, key); // must not crash / touch memory
}

int main(void) {
    test_xor_spec();
    test_involution();
    test_zero_len();
    printf("test_mask: all passed\n");
    return 0;
}
