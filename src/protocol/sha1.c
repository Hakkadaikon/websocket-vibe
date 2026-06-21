// SHA-1 (RFC3174 / FIPS-180)。libc に依存せず、ループだけで完結させる。
#include "protocol/sha1.h"

static u32 rotl(u32 x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

// ラウンド (0..3) ごとの定数と f 関数。ラウンド番号で選ぶことで、
// 呼び出し側に範囲判定の分岐を持たせない。
static const u32 SHA1_K[4] = {0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u};

static u32 sha1_f0(u32 b, u32 c, u32 d) {
    return (b & c) | (~b & d);
}

static u32 sha1_f1(u32 b, u32 c, u32 d) {
    return b ^ c ^ d;
}

static u32 sha1_f2(u32 b, u32 c, u32 d) {
    return (b & c) | (b & d) | (c & d);
}

static u32 sha1_f(unsigned round, u32 b, u32 c, u32 d) {
    u32 (*const fns[4])(u32, u32, u32) = {sha1_f0, sha1_f1, sha1_f2, sha1_f1};
    return fns[round](b, c, d);
}

typedef struct {
    u32 a, b, c, d, e;
} sha1_state;

static void sha1_expand(u32 w[80], const u8 *p) {
    for (size_t i = 0; i < 16; i++)
        w[i] = ((u32) p[4 * i] << 24) | ((u32) p[4 * i + 1] << 16) | ((u32) p[4 * i + 2] << 8) |
               (u32) p[4 * i + 3];
    for (unsigned i = 16; i < 80; i++)
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
}

static void sha1_step(sha1_state *s, unsigned round, u32 wt) {
    u32 tmp = rotl(s->a, 5) + sha1_f(round, s->b, s->c, s->d) + s->e + wt + SHA1_K[round];
    s->e = s->d;
    s->d = s->c;
    s->c = rotl(s->b, 30);
    s->b = s->a;
    s->a = tmp;
}

static void sha1_round(sha1_state *s, unsigned round, const u32 w[80]) {
    for (unsigned t = round * 20; t < round * 20 + 20; t++)
        sha1_step(s, round, w[t]);
}

static void sha1_block(u32 h[5], const u8 *p) {
    u32 w[80];
    sha1_expand(w, p);
    sha1_state s = {h[0], h[1], h[2], h[3], h[4]};
    for (unsigned round = 0; round < 4; round++)
        sha1_round(&s, round, w);
    h[0] += s.a;
    h[1] += s.b;
    h[2] += s.c;
    h[3] += s.d;
    h[4] += s.e;
}

static size_t sha1_blocks(u32 h[5], const u8 *data, size_t len) {
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha1_block(h, data + i);
    return i;
}

static void sha1_fill(u8 *buf, size_t from, size_t to) {
    for (size_t j = from; j < to; j++)
        buf[j] = 0;
}

// ブロックの末尾 8 バイトに 64 ビットのビット長をビッグエンディアンで書き込む。
static void sha1_put_len(u8 *buf, size_t total, u64 len) {
    u64 bits = len * 8;
    for (unsigned k = 0; k < 8; k++)
        buf[total - 1 - k] = (u8) (bits >> (8 * k));
}

// 最終のパディング済みブロックを buf に組み立てる。合計長 (64 または 128) を返す。
static size_t sha1_pad(u8 buf[128], const u8 *tail, size_t rem, u64 len) {
    for (size_t j = 0; j < rem; j++)
        buf[j] = tail[j];
    buf[rem] = 0x80;
    size_t total = (rem + 1 <= 56) ? 64 : 128;
    sha1_fill(buf, rem + 1, total - 8);
    sha1_put_len(buf, total, len);
    return total;
}

static void sha1_emit(const u32 h[5], u8 out[20]) {
    for (size_t k = 0; k < 5; k++) {
        out[4 * k] = (u8) (h[k] >> 24);
        out[4 * k + 1] = (u8) (h[k] >> 16);
        out[4 * k + 2] = (u8) (h[k] >> 8);
        out[4 * k + 3] = (u8) h[k];
    }
}

void ws_sha1(const u8 *data, size_t len, u8 out[20]) {
    u32 h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    size_t i = sha1_blocks(h, data, len);

    u8 buf[128];
    size_t total = sha1_pad(buf, data + i, len - i, (u64) len);
    sha1_block(h, buf);
    if (total == 128)
        sha1_block(h, buf + 64);

    sha1_emit(h, out);
}
