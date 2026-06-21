// ws — RFC6455 §5 frame model (public).
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

// Parsed frame header (the bytes before the payload).
typedef struct {
    bool fin;
    bool rsv1, rsv2, rsv3;
    u8 opcode;
    bool masked;
    u64 payload_len;
    u8 mask_key[4];
    size_t header_len; // total header size in bytes (2..14)
} ws_frame_header;

// Parse result codes.
typedef enum {
    WS_PARSE_OK = 0,
    WS_PARSE_NEED_MORE = 1, // not enough bytes yet
    WS_PARSE_ERROR = 2,     // protocol violation (e.g. non-minimal length)
} ws_parse_status;

#endif // WS_FRAME_H
