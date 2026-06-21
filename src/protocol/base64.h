// ws protocol — base64 encode (RFC4648) for the handshake accept key.
#ifndef WS_PROTOCOL_BASE64_H
#define WS_PROTOCOL_BASE64_H

#include "ws/types.h"

// Encode `len` bytes into `out` (must hold 4*ceil(len/3)+1 bytes incl. NUL).
// Returns the number of characters written (excluding NUL).
size_t ws_base64_encode(const u8 *data, size_t len, char *out);

#endif // WS_PROTOCOL_BASE64_H
