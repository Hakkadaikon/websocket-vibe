// SHA-1 (RFC3174 / FIPS-180). No libc: self-contained loops.
#include "protocol/sha1.h"

static u32 rotl(u32 x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

static u32 sha1_f(unsigned t, u32 b, u32 c, u32 d) {
    if (t < 20)
        return (b & c) | (~b & d);
    if (t < 40)
        return b ^ c ^ d;
    if (t < 60)
        return (b & c) | (b & d) | (c & d);
    return b ^ c ^ d;
}

static u32 sha1_k(unsigned t) {
    if (t < 20)
        return 0x5A827999u;
    if (t < 40)
        return 0x6ED9EBA1u;
    if (t < 60)
        return 0x8F1BBCDCu;
    return 0xCA62C1D6u;
}

static void sha1_block(u32 h[5], const u8 *p) {
    u32 w[80];
    for (size_t i = 0; i < 16; i++)
        w[i] = ((u32) p[4 * i] << 24) | ((u32) p[4 * i + 1] << 16) | ((u32) p[4 * i + 2] << 8) |
               (u32) p[4 * i + 3];
    for (unsigned i = 16; i < 80; i++)
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (unsigned t = 0; t < 80; t++) {
        u32 tmp = rotl(a, 5) + sha1_f(t, b, c, d) + e + w[t] + sha1_k(t);
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void ws_sha1(const u8 *data, size_t len, u8 out[20]) {
    u32 h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    // Process all full 64-byte blocks.
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha1_block(h, data + i);

    // Final padded block(s).
    u8 buf[128];
    size_t rem = len - i;
    for (size_t j = 0; j < rem; j++)
        buf[j] = data[i + j];
    buf[rem] = 0x80;
    size_t total = (rem + 1 <= 56) ? 64 : 128;
    for (size_t j = rem + 1; j < total - 8; j++)
        buf[j] = 0;

    u64 bits = (u64) len * 8;
    for (unsigned k = 0; k < 8; k++)
        buf[total - 1 - k] = (u8) (bits >> (8 * k));

    sha1_block(h, buf);
    if (total == 128)
        sha1_block(h, buf + 64);

    for (size_t k = 0; k < 5; k++) {
        out[4 * k] = (u8) (h[k] >> 24);
        out[4 * k + 1] = (u8) (h[k] >> 16);
        out[4 * k + 2] = (u8) (h[k] >> 8);
        out[4 * k + 3] = (u8) h[k];
    }
}
