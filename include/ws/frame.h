// ws — RFC6455 §5 フレームモデル (公開)。
#ifndef WS_FRAME_H
#define WS_FRAME_H

#include "ws/types.h"

typedef enum {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA,
} ws_opcode;

// 解析済みのフレームヘッダ (ペイロードより前のバイト列)。
typedef struct {
    bool fin;
    bool rsv1, rsv2, rsv3;
    u8 opcode;
    bool masked;
    u64 payload_len;
    u8 mask_key[4];
    size_t header_len; // ヘッダ全体のバイト数 (2..14)
} ws_frame_header;

// 解析結果コード。
typedef enum {
    WS_PARSE_OK = 0,
    WS_PARSE_NEED_MORE = 1, // まだバイトが足りない
    WS_PARSE_ERROR = 2,     // プロトコル違反 (例: 最小でない長さ表現)
} ws_parse_status;

#endif // WS_FRAME_H のガード終端
