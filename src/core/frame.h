// ws core — frame header parse/build (RFC6455 §5.2).
// Length codec proven in Lean WsProof.LengthCodec:
//   encode: <=125 -> 1 byte; 126 -> +2 BE; 127 -> +8 BE.
//   decode(encode n) = n  (roundtrip), encode injective (canonical/minimal).
#ifndef WS_CORE_FRAME_H
#define WS_CORE_FRAME_H

#include "ws/frame.h"
#include "ws/types.h"

// Parse a frame header from `buf` (`len` available bytes).
// On WS_PARSE_OK, `out` is filled and out->header_len bytes were consumed.
// Returns NEED_MORE if more bytes are required, ERROR on protocol violation
// (non-minimal length encoding, or payload_len high bit set).
ws_parse_status ws_frame_parse_header(const u8 *buf, size_t len, ws_frame_header *out);

// Serialize a frame header into `buf` (capacity `cap`). Returns header byte
// count, or 0 if `cap` too small. `mask_key` may be NULL when masked==false.
// Always emits the minimal (canonical) length form.
size_t ws_frame_build_header(u8 *buf, size_t cap, bool fin, u8 opcode, bool masked,
                             const u8 mask_key[4], u64 payload_len);

#endif // WS_CORE_FRAME_H
