// ws — public SDK (RFC6455). Sans-IO core: you feed bytes in and pump bytes
// out; no syscalls in this layer, so it runs freestanding and is fully testable.
// A separate server loop (src/sdk/server.c) wires this to sockets.
#ifndef WS_WS_H
#define WS_WS_H

#include "ws/frame.h"
#include "ws/types.h"

#define WS_MAX_MESSAGE (1u << 20) // 1 MiB default aggregation cap

typedef enum {
    WS_ROLE_SERVER = 0, // expects masked client frames; sends unmasked
    WS_ROLE_CLIENT = 1, // sends masked frames; expects unmasked
} ws_role;

typedef enum {
    WS_ST_OPEN    = 0,
    WS_ST_CLOSING = 1,
    WS_ST_CLOSED  = 2,
} ws_conn_state;

// Events surfaced to the caller after ws_conn_recv().
typedef enum {
    WS_EV_NONE    = 0, // need more bytes
    WS_EV_MESSAGE = 1, // a complete data message is ready (text/binary)
    WS_EV_PING    = 2,
    WS_EV_PONG    = 3,
    WS_EV_CLOSE   = 4, // peer initiated close
    WS_EV_ERROR   = 5, // protocol violation; connection must close
} ws_event_type;

typedef struct {
    ws_event_type type;
    u8            opcode;     // WS_OP_TEXT / WS_OP_BINARY for MESSAGE
    const u8     *data;       // payload (into the conn's message buffer)
    size_t        len;
    u16           close_code; // for WS_EV_CLOSE
} ws_event;

// Opaque connection. Storage is caller-provided (no malloc in freestanding).
// Use WS_CONN_SIZE bytes, WS_CONN_ALIGN alignment.
#define WS_CONN_SIZE  256
#define WS_CONN_ALIGN 8
typedef struct {
    _Alignas(WS_CONN_ALIGN) u8 _opaque[WS_CONN_SIZE];
} ws_conn;

// --- lifecycle ---
// Initialize `c` in place. `msg_buf`/`msg_cap` back the message-aggregation
// buffer (must outlive the connection). Returns false if storage too small.
bool          ws_conn_init(ws_conn *c, ws_role role, u8 *msg_buf, size_t msg_cap);
ws_conn_state ws_conn_status(const ws_conn *c);

// --- inbound: feed received bytes, drain events ---
// Feed up to `len` bytes; returns bytes consumed. Call ws_conn_poll() until it
// yields WS_EV_NONE to drain all complete events from the fed bytes.
size_t        ws_conn_recv(ws_conn *c, const u8 *buf, size_t len);
ws_event_type ws_conn_poll(ws_conn *c, ws_event *ev);

// --- outbound: build frames into caller buffer, returns bytes written (0=fail) ---
size_t ws_send_message(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out,
                       size_t cap);
size_t ws_send_ping(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap);
size_t ws_send_pong(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap);
size_t ws_send_close(ws_conn *c, u16 code, u8 *out, size_t cap);

#endif // WS_WS_H
