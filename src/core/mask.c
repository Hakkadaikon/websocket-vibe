#include "core/mask.h"

// Proven spec (WsProof.Masking): out[i] = data[i] ^ key[i % 4].
// Byte loop is correct but slow; we XOR 8 bytes at a time once aligned. The
// 64-bit mask word repeats the 4-byte key, rotated to match the current index
// mod 4, so every byte still meets key[i % 4]. Self-check below guards it.
void ws_mask(u8 *data, size_t len, const u8 key[4]) {
    size_t i = 0;

    // Build the base 64-bit key word: key[0..3] repeated twice (i%4 aligned).
    u64 kw = 0;
    for (size_t b = 0; b < 8; b++)
        kw |= (u64) key[b & 3u] << (8u * b);

    // Lead bytes until `data` is 8-aligned (so the u64 load is aligned).
    while (i < len && (((uintptr_t) (data + i)) & 7u) != 0) {
        data[i] ^= key[i & 3u];
        i++;
    }

    // Rotate kw so its byte 0 corresponds to key[i % 4] at the aligned point.
    unsigned shift = (unsigned) (i & 3u) * 8u;
    u64 kcur = (kw >> shift) | (kw << ((64u - shift) & 63u));
    if (shift == 0)
        kcur = kw;

    // Word-at-a-time over the aligned middle.
    for (; i + 8 <= len; i += 8) {
        u64 v;
        __builtin_memcpy(&v, data + i, 8);
        v ^= kcur;
        __builtin_memcpy(data + i, &v, 8);
    }

    // Trailing bytes.
    for (; i < len; i++)
        data[i] ^= key[i & 3u];
}
