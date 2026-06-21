// フリースタンディングな mem* — clang がビルトインからこれらの呼び出しを生成し
// うるため、必ず存在させる必要がある。バイト単位で実装し、速度より正しさを優先する
// (ホットパスはベンチ層で最適化する)。
#include "platform/mem.h"

void *ws_memcpy(void *dst, const void *src, size_t n) {
    u8 *d = (u8 *) dst;
    const u8 *s = (const u8 *) src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

// 末尾から前方へコピー (dst > src の後方オーバーラップを安全に扱う)。
static void copy_backward(u8 *d, const u8 *s, size_t n) {
    for (size_t i = n; i-- > 0;)
        d[i] = s[i];
}

// dst <= src なら昇順 (ws_memcpy)、dst > src なら降順でコピーする。
// 集約バッファ内でペイロードを先頭へ詰め直すときの自己オーバーラップ対策。
void *ws_memmove(void *dst, const void *src, size_t n) {
    u8 *d = (u8 *) dst;
    const u8 *s = (const u8 *) src;
    if (d <= s)
        return ws_memcpy(dst, src, n);
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
