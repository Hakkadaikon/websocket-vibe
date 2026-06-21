// ws プロトコル — ハンドシェイクの accept キー用の SHA-1 (RFC3174, RFC6455 §4.2.2)。
// 標準アルゴリズムを FIPS/RFC のテストベクタで検証済み (test_handshake を参照)。
#ifndef WS_PROTOCOL_SHA1_H
#define WS_PROTOCOL_SHA1_H

#include "ws/types.h"

#define WS_SHA1_DIGEST_LEN 20 // SHA-1 ダイジェストのバイト数 (RFC3174)

// `data` の SHA-1 ダイジェスト (WS_SHA1_DIGEST_LEN バイト) を計算し `out` に書き込む。
void ws_sha1(const u8 *data, size_t len, u8 out[WS_SHA1_DIGEST_LEN]);

#endif // WS_PROTOCOL_SHA1_H のガード終端
