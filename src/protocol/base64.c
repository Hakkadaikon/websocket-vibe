// base64 encode (RFC4648 standard alphabet). No libc.
#include "protocol/base64.h"

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t ws_base64_encode(const u8 *data, size_t len, char *out) {
    size_t o = 0;
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        u32 v = ((u32) data[i] << 16) | ((u32) data[i + 1] << 8) | data[i + 2];
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = B64[(v >> 6) & 0x3F];
        out[o++] = B64[v & 0x3F];
    }
    size_t rem = len - i; // 0, 1, or 2
    if (rem == 1) {
        u32 v = (u32) data[i] << 16;
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        u32 v = ((u32) data[i] << 16) | ((u32) data[i + 1] << 8);
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = B64[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}
