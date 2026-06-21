// base64 エンコード (RFC4648 標準アルファベット)。libc に依存しない。
#include "protocol/base64.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 24 ビット値 v を 4 文字のグループ 1 つとして出力する。
static void b64_quad(char *out, u32 v) {
    out[0] = B64[(v >> 18) & 0x3F];
    out[1] = B64[(v >> 12) & 0x3F];
    out[2] = B64[(v >> 6) & 0x3F];
    out[3] = B64[v & 0x3F];
}

// 1/2 バイトの端数を '=' パディング付きでエンコードする。書き込んだ文字数を返す。
static size_t b64_tail2(char *out, const u8 *data, size_t rem) {
    u32 v = (u32) data[0] << 16;
    v |= (rem == 2) ? ((u32) data[1] << 8) : 0u;
    b64_quad(out, v);
    out[3] = '=';
    if (rem == 1)
        out[2] = '='; // 入力 1 バイトならパディングは 2 文字
    return 4;
}

// 0/1/2 バイトの端数をエンコードする。書き込んだ文字数を返す。
static size_t b64_tail(char *out, const u8 *data, size_t rem) {
    return rem ? b64_tail2(out, data, rem) : 0;
}

size_t ws_base64_encode(const u8 *data, size_t len, char *out) {
    size_t o = 0;
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        u32 v = ((u32) data[i] << 16) | ((u32) data[i + 1] << 8) | data[i + 2];
        b64_quad(out + o, v);
        o += 4;
    }
    o += b64_tail(out + o, data + i, len - i);
    out[o] = 0;
    return o;
}
