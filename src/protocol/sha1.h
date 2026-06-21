// ws プロトコル — ハンドシェイクの accept キー用の SHA-1 (RFC3174, RFC6455 §4.2.2)。
// 標準アルゴリズムを FIPS/RFC のテストベクタで検証済み (test_handshake を参照)。
#ifndef WS_PROTOCOL_SHA1_H
#define WS_PROTOCOL_SHA1_H

#include "ws/types.h"

// `data` の 20 バイトの SHA-1 ダイジェストを計算し `out` に書き込む。
void ws_sha1(const u8 *data, size_t len, u8 out[20]);

#endif // WS_PROTOCOL_SHA1_H のガード終端
