#include "core/frame.h"

// RFC6455 §5.2 の長さコーデック境界 (Lean WsProof.LengthCodec と対応)。
enum {
    WS_LEN7_MAX = 125,     // この値以下は len7 に直接符号化
    WS_LEN7_16BIT = 126,   // len7 マーカー: 続く 2 バイト BE が実長
    WS_LEN7_64BIT = 127,   // len7 マーカー: 続く 8 バイト BE が実長
    WS_LEN16_MAX = 0xFFFF, // 2 バイト形式で表せる最大長 (65535)
};

// --- ビッグエンディアン補助 (Lean モデル fromBE / byteAt に対応) ---

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

// --- パース ---

// 2 バイト拡張長 (len7 == 126)。最小符号化を強制する。
static ws_parse_status parse_len16(const u8 *buf, size_t len, u64 *out_len, size_t *extra) {
    if (len < 4)
        return WS_PARSE_NEED_MORE;
    u64 v = read_be(buf + 2, 2);
    if (v < WS_LEN7_16BIT) // 非最小: 1 バイト形式で表せる値はそちらを使うべき
        return WS_PARSE_ERROR;
    *out_len = v;
    *extra = 2;
    return WS_PARSE_OK;
}

// デコード済みの 8 バイト長を検証する: 最上位ビットが 0 (RFC6455 §5.2) で、
// かつ最小符号化であること (収まる値は 2 バイト形式を使うべき)。
static ws_parse_status check_len64(u64 v) {
    if (v & 0x8000000000000000u) // 最上位ビットは 0 でなければならない (RFC6455 §5.2)
        return WS_PARSE_ERROR;
    if (v <= WS_LEN16_MAX) // 非最小: 2 バイト形式で表せる値はそちらを使うべき
        return WS_PARSE_ERROR;
    return WS_PARSE_OK;
}

// 8 バイト拡張長 (len7 == 127)。最小符号化と予約された最上位ビット
// (RFC6455 §5.2) を強制する。
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

// 長さフィールドをデコードする。*extra に 2 バイト目以降の追加バイト数を設定する。
// パース状態を返す。最小 (正準) 符号化を強制する。
static ws_parse_status parse_len(const u8 *buf, size_t len, u8 len7, u64 *out_len, size_t *extra) {
    if (len7 < WS_LEN7_16BIT) {
        *out_len = len7;
        *extra = 0;
        return WS_PARSE_OK;
    }
    if (len7 == WS_LEN7_16BIT)
        return parse_len16(buf, len, out_len, extra);
    return parse_len64(buf, len, out_len, extra);
}

// 4 バイトのマスクキーを src から埋める。src が null のときはゼロ埋めする。
static void fill_mask_key(u8 dst[4], const u8 *src) {
    for (size_t i = 0; i < 4; i++)
        dst[i] = src ? src[i] : 0;
}

// マスクキーが占めるヘッダバイト数 (マスクありなら 4、なしなら 0)。
static size_t mask_len(bool masked) {
    return masked ? 4u : 0u;
}

// 先頭ヘッダバイト (FIN/RSV/opcode) を out に展開する。
static void unpack_b0(ws_frame_header *out, u8 b0) {
    out->fin = (b0 & 0x80u) != 0;
    out->rsv1 = (b0 & 0x40u) != 0;
    out->rsv2 = (b0 & 0x20u) != 0;
    out->rsv3 = (b0 & 0x10u) != 0;
    out->opcode = b0 & 0x0Fu;
}

// 検証済みヘッダから out を埋める: b0 のフラグ、マスクキー、長さ、合計。
// `extra` は 1 バイト目以降の長さフィールドのバイト数、`plen` はペイロード長。
static void fill_header(ws_frame_header *out, const u8 *buf, bool masked, size_t extra, u64 plen) {
    const u8 *key = masked ? buf + 2 + extra : NULL;
    unpack_b0(out, buf[0]);
    out->masked = masked;
    out->payload_len = plen;
    out->header_len = 2 + extra + mask_len(masked);
    fill_mask_key(out->mask_key, key);
}

// 長さをデコードし、ヘッダ全体 (マスクキー含む) がバッファ済みか検証する。
// OK のとき *plen/*extra が設定され、*masked がマスクフラグを反映する。
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

// --- ビルド ---

// あるペイロード長に必要な追加長バイト数 (先頭 2 ヘッダバイトを除く)。
static size_t len_extra(u64 payload_len) {
    if (payload_len <= WS_LEN7_MAX)
        return 0;
    if (payload_len <= WS_LEN16_MAX)
        return 2;
    return 8;
}

// 長さフィールドを buf のインデックス 1 から書き出す。(任意の) マスクキーの
// 直前までのヘッダ総バイト数を返す。常に最小形式で出力する。
static size_t build_len(u8 *buf, u64 payload_len) {
    if (payload_len <= WS_LEN7_MAX) {
        buf[1] = (u8) payload_len;
        return 2;
    }
    if (payload_len <= WS_LEN16_MAX) {
        buf[1] = WS_LEN7_16BIT;
        write_be(buf + 2, 2, payload_len);
        return 4;
    }
    buf[1] = WS_LEN7_64BIT;
    write_be(buf + 2, 8, payload_len);
    return 10;
}

// マスクフラグとキーをインデックス n に追加する。新しいヘッダ長を返す。
static size_t append_mask(u8 *buf, size_t n, const u8 mask_key[4]) {
    buf[1] |= 0x80u;
    for (size_t i = 0; i < 4; i++)
        buf[n + i] = mask_key[i];
    return n + 4;
}

// 先頭ヘッダバイト: FIN フラグと 4 ビットの opcode。
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
