// ws protocol — RFC6455 §4 opening handshake.
#ifndef WS_PROTOCOL_HANDSHAKE_H
#define WS_PROTOCOL_HANDSHAKE_H

#include "ws/types.h"

#define WS_ACCEPT_KEY_LEN 28 // base64(SHA1(...)) = 28 chars

// Compute Sec-WebSocket-Accept for a given Sec-WebSocket-Key (RFC6455 §4.2.2).
// `key` is the raw header value (24 base64 chars); writes 28 chars + NUL.
void ws_handshake_accept(const char *key, size_t key_len, char out[WS_ACCEPT_KEY_LEN + 1]);

// Extract the Sec-WebSocket-Key value from a raw HTTP request header block.
// Returns its length and sets *val, or 0 if absent. Case-insensitive header name.
size_t ws_handshake_find_key(const char *req, size_t len, const char **val);

// Build the 101 Switching Protocols response for `accept` into `out` (cap bytes).
// Returns response length or 0 if too small.
size_t ws_handshake_response(const char *accept, char *out, size_t cap);

#endif // WS_PROTOCOL_HANDSHAKE_H
