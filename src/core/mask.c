#include "core/mask.h"

// 証明済み仕様 (WsProof.Masking): out[i] = data[i] ^ key[i % 4]。
// バイト単位ループは正しいが遅いため、アラインメント後は 8 バイトずつ XOR する。
// 64 ビットのマスクワードは 4 バイトキーを繰り返したもので、現在のインデックスの
// mod 4 に合わせて回転させるため、各バイトは依然として key[i % 4] を満たす。
// 下の自己検査がこれを保証する。
// 基底となる 64 ビットキーワード: key[0..3] を 2 回繰り返す (i%4 にアライン)。
static inline u64 key_word(const u8 key[4]) {
    u64 kw = 0;
    for (size_t b = 0; b < 8; b++)
        kw |= (u64) key[b & 3u] << (8u * b);
    return kw;
}

// `r` ビット右回転 (0 <= r < 64)。r==0 での 64 ビットシフトの未定義動作を避ける。
static inline u64 rotr64(u64 x, unsigned r) {
    return (x >> r) | (x << ((64u - r) & 63u));
}

// 1 バイトを key[i % 4] と XOR する。
static inline void mask_byte(u8 *data, size_t i, const u8 key[4]) {
    data[i] ^= key[i & 3u];
}

// `data + i` が 8 バイト境界に乗るまで先頭バイトをマスクする。新しいインデックスを返す。
static inline size_t mask_lead(u8 *data, size_t len, const u8 key[4]) {
    size_t i = 0;
    while (i < len && (((uintptr_t) (data + i)) & 7u) != 0) {
        mask_byte(data, i, key);
        i++;
    }
    return i;
}

// アラインされた中間部をワード単位で処理する。新しいインデックスを返す。
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

    // アライン地点で kw のバイト 0 が key[i % 4] に対応するよう kw を回転する。
    u64 kcur = rotr64(kw, (unsigned) (i & 3u) * 8u);
    i = mask_words(data, len, i, kcur);

    // 末尾の端数バイト。
    for (; i < len; i++)
        mask_byte(data, i, key);
}
