// フレームヘッダコーデックのホワイトボックステスト。Lean WsProof.LengthCodec に対応:
// 3 通りの長さ形式すべてでの build/parse ラウンドトリップと、
// 最小でない(非正規)符号化の拒否を検証する。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/frame.c"

static void roundtrip(u64 len, size_t expect_hdr) {
    u8 key[4] = {0x12, 0x34, 0x56, 0x78};
    u8 buf[14];
    size_t n = ws_frame_build_header(buf, sizeof buf, true, WS_OP_BINARY, true, key, len);
    assert(n == expect_hdr);

    ws_frame_header h;
    ws_parse_status st = ws_frame_parse_header(buf, n, &h);
    assert(st == WS_PARSE_OK);
    assert(h.fin == true);
    assert(h.opcode == WS_OP_BINARY);
    assert(h.masked == true);
    assert(h.payload_len == len);
    assert(h.header_len == n);
    assert(memcmp(h.mask_key, key, 4) == 0);
}

static void test_roundtrip_all_forms(void) {
    roundtrip(0, 6);      // 1 バイト長 + マスク 4
    roundtrip(125, 6);    // 1 バイト形式の境界
    roundtrip(126, 8);    // 2 バイト形式 + マスク
    roundtrip(65535, 8);  // 2 バイト形式の境界
    roundtrip(65536, 14); // 8 バイト形式 + マスク
    roundtrip(1u << 20, 14);
}

static void test_unmasked_header_sizes(void) {
    u8 buf[14];
    assert(ws_frame_build_header(buf, sizeof buf, true, WS_OP_TEXT, false, NULL, 5) == 2);
    assert(ws_frame_build_header(buf, sizeof buf, true, WS_OP_TEXT, false, NULL, 126) == 4);
    assert(ws_frame_build_header(buf, sizeof buf, true, WS_OP_TEXT, false, NULL, 70000) == 10);
}

static void test_need_more(void) {
    u8 buf[14];
    size_t n =
        ws_frame_build_header(buf, sizeof buf, true, WS_OP_BINARY, true, (u8[]){1, 2, 3, 4}, 65536);
    ws_frame_header h;
    // ヘッダより短いどの接頭部で切られても NEED_MORE を返さねばならない。
    for (size_t k = 0; k < n; k++)
        assert(ws_frame_parse_header(buf, k, &h) == WS_PARSE_NEED_MORE);
    assert(ws_frame_parse_header(buf, n, &h) == WS_PARSE_OK);
}

static void test_reject_non_minimal(void) {
    ws_frame_header h;
    // 126 形式で長さ <= 125 を符号化するのは非正規 -> ERROR。
    u8 bad2[] = {0x82, 126, 0x00, 0x10}; // 2 バイト形式で len=16
    assert(ws_frame_parse_header(bad2, sizeof bad2, &h) == WS_PARSE_ERROR);
    // 127 形式で長さ <= 65535 を符号化するのは非正規 -> ERROR。
    u8 bad8[] = {0x82, 127, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    assert(ws_frame_parse_header(bad8, sizeof bad8, &h) == WS_PARSE_ERROR);
}

static void test_reject_high_bit(void) {
    ws_frame_header h;
    // MSB が立った 64 ビット長は RFC6455 §5.2 で不正。
    u8 bad[] = {0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 1};
    assert(ws_frame_parse_header(bad, sizeof bad, &h) == WS_PARSE_ERROR);
}

int main(void) {
    test_roundtrip_all_forms();
    test_unmasked_header_sizes();
    test_need_more();
    test_reject_non_minimal();
    test_reject_high_bit();
    printf("test_frame: all passed\n");
    return 0;
}
