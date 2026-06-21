// フリースタンディングな mem* — clang がビルトインからこれらの呼び出しを生成し
// うるため、必ず存在させる必要がある。バイト単位で実装し、速度より正しさを優先する
// (ホットパスはベンチ層で最適化する)。
#include "platform/mem.h"

// 先頭から昇順にコピー。dst <= src の重なりなら安全 (ws_memmove の前方経路)。
static void copy_forward(u8 *d, const u8 *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

// 末尾から降順にコピー。dst > src の後方オーバーラップを安全に扱う。
static void copy_backward(u8 *d, const u8 *s, size_t n) {
    for (size_t i = n; i-- > 0;)
        d[i] = s[i];
}

// 区間 [d,d+n) と [s,s+n) が重なるか。重なるなら memcpy は不正 (memmove を使う)。
static bool ranges_overlap(const u8 *d, const u8 *s, size_t n) {
    return d < s + n && s < d + n;
}

// デバッグビルドでのみ、memcpy のオーバーラップ誤用を即トラップする。
// ASan は自作 ws_memcpy を傍受しないため、これが唯一の自動検出網になる。
// NDEBUG(リリース)では空 — ホットパスは無コスト。libc 非依存 (__builtin_trap)。
static void assert_no_overlap(const u8 *d, const u8 *s, size_t n) {
#ifndef NDEBUG
    if (ranges_overlap(d, s, n))
        __builtin_trap();
#else
    (void) d, (void) s, (void) n;
#endif
}

void *ws_memcpy(void *dst, const void *src, size_t n) {
    u8 *d = (u8 *) dst;
    const u8 *s = (const u8 *) src;
    assert_no_overlap(d, s, n);
    copy_forward(d, s, n);
    return dst;
}

// dst <= src なら昇順、dst > src なら降順でコピーする。集約バッファ内で
// ペイロードを先頭へ詰め直すときの自己オーバーラップに対応する。
void *ws_memmove(void *dst, const void *src, size_t n) {
    u8 *d = (u8 *) dst;
    const u8 *s = (const u8 *) src;
    if (d <= s)
        copy_forward(d, s, n);
    else
        copy_backward(d, s, n);
    return dst;
}

void *ws_memset(void *dst, int c, size_t n) {
    u8 *d = (u8 *) dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (u8) c;
    return dst;
}

int ws_memcmp(const void *a, const void *b, size_t n) {
    const u8 *pa = (const u8 *) a;
    const u8 *pb = (const u8 *) b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return (int) pa[i] - (int) pb[i];
    }
    return 0;
}
