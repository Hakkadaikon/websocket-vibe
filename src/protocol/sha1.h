// ws protocol — SHA-1 (RFC3174) for the handshake accept key (RFC6455 §4.2.2).
// Standard algorithm verified against FIPS/RFC test vectors (see test_handshake).
#ifndef WS_PROTOCOL_SHA1_H
#define WS_PROTOCOL_SHA1_H

#include "ws/types.h"

// Compute the 20-byte SHA-1 digest of `data` into `out`.
void ws_sha1(const u8 *data, size_t len, u8 out[20]);

#endif // WS_PROTOCOL_SHA1_H
