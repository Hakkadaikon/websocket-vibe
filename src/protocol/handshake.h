// ws プロトコル — RFC6455 §4 オープニングハンドシェイク。
#ifndef WS_PROTOCOL_HANDSHAKE_H
#define WS_PROTOCOL_HANDSHAKE_H

#include "ws/types.h"

#define WS_ACCEPT_KEY_LEN 28 // base64(SHA1(...)) は 28 文字

// 与えられた Sec-WebSocket-Key から Sec-WebSocket-Accept を計算する (RFC6455 §4.2.2)。
// `key` は生のヘッダ値 (base64 24 文字)。28 文字 + NUL を書き込む。
void ws_handshake_accept(const char *key, size_t key_len, char out[WS_ACCEPT_KEY_LEN + 1]);

// 生の HTTP リクエストヘッダブロックから Sec-WebSocket-Key の値を取り出す。
// 値の長さを返し *val を設定する。存在しなければ 0。ヘッダ名は大文字小文字を区別しない。
size_t ws_handshake_find_key(const char *req, size_t len, const char **val);

// `accept` に対する 101 Switching Protocols レスポンスを `out` (cap バイト) に組み立てる。
// レスポンス長を返す。領域が小さすぎる場合は 0。
size_t ws_handshake_response(const char *accept, char *out, size_t cap);

#endif // WS_PROTOCOL_HANDSHAKE_H のガード終端
