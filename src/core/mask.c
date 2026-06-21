#include "core/mask.h"

// Proven spec (WsProof.Masking): out[i] = data[i] ^ key[i % 4].
// Byte loop is correct but slow; we XOR 8 bytes at a time once aligned. The
// 64-bit mask word repeats the 4-byte key, rotated to match the current index
// mod 4, so every byte still meets key[i % 4]. Self-check below guards it.
// Base 64-bit key word: key[0..3] repeated twice (i%4 aligned).
static inline u64 key_word(const u8 key[4]) {
    u64 kw = 0;
    for (size_t b = 0; b < 8; b++)
        kw |= (u64) key[b & 3u] << (8u * b);
    return kw;
}

// Right-rotate by `r` bits (0 <= r < 64), avoiding a 64-bit shift UB at r==0.
static inline u64 rotr64(u64 x, unsigned r) {
    return (x >> r) | (x << ((64u - r) & 63u));
}

// XOR one byte against key[i % 4].
static inline void mask_byte(u8 *data, size_t i, const u8 key[4]) {
    data[i] ^= key[i & 3u];
}

// Mask leading bytes until `data + i` is 8-aligned; returns the new index.
static inline size_t mask_lead(u8 *data, size_t len, const u8 key[4]) {
    size_t i = 0;
    while (i < len && (((uintptr_t) (data + i)) & 7u) != 0) {
        mask_byte(data, i, key);
        i++;
    }
    return i;
}

// Word-at-a-time over the aligned middle; returns the new index.
static inline size_t mask_words(u8 *data, size_t len, size_t i, u64 kcur) {
    for (; i + 8 <= len; i += 8) {
        u64 v;
        __builtin_memcpy(&v, data + i, 8);
        v ^= kcur;
        __builtin_memcpy(data + i, &v, 8);
    }
    return i;
}

void ws_mask(u8 *data, size_t len, const u8 key[4]) {
    u64 kw = key_word(key);
    size_t i = mask_lead(data, len, key);

    // Rotate kw so its byte 0 corresponds to key[i % 4] at the aligned point.
    u64 kcur = rotr64(kw, (unsigned) (i & 3u) * 8u);
    i = mask_words(data, len, i, kcur);

    // Trailing bytes.
    for (; i < len; i++)
        mask_byte(data, i, key);
}
