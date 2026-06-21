// ws プロトコル — ハンドシェイクの accept キー用の base64 エンコード (RFC4648)。
#ifndef WS_PROTOCOL_BASE64_H
#define WS_PROTOCOL_BASE64_H

#include "ws/types.h"

// `len` バイトを `out` にエンコードする (NUL を含め 4*ceil(len/3)+1 バイト分の領域が必要)。
// 書き込んだ文字数 (NUL を除く) を返す。
size_t ws_base64_encode(const u8 *data, size_t len, char *out);

#endif // WS_PROTOCOL_BASE64_H のガード終端
