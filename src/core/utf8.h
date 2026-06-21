// ws core — RFC3629 UTF-8 検証 (RFC6455 §8.1 テキストフレーム)。
// 分岐構造は Lean WsProof.Utf8 で証明済み (validate_correct:
// accept <-> WellFormed)。状態を持つ変種により、断片化したテキストフレームを
// またいだ検証が可能。
#ifndef WS_CORE_UTF8_H
#define WS_CORE_UTF8_H

#include "ws/types.h"

// 逐次検証の状態。開始時はゼロ初期化する。
typedef struct {
    u8 remaining; // まだ期待される継続バイト数 (0 = 境界上)
    u8 lo, hi;    // 次の継続バイトに許される範囲
} ws_utf8_state;

// `len` バイトを投入する。最初の不正なバイトで false を返す。
// 状態は保持され、1 つのシーケンスが複数回の呼び出しにまたがってよい。
bool ws_utf8_feed(ws_utf8_state *s, const u8 *buf, size_t len);

// 未完の部分シーケンスが残っていなければ真 (最終フラグメントの後に呼ぶ)。
bool ws_utf8_complete(const ws_utf8_state *s);

// バッファ全体を一度に検証する (整形式かつ末尾に未完シーケンスがない)。
bool ws_utf8_valid(const u8 *buf, size_t len);

#endif // WS_CORE_UTF8_H のガード終端
