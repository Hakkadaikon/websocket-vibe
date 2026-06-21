// ws core — RFC6455 §5.3 payload masking.
// Proven (Lean WsProof.Masking): transformed[i] = data[i] XOR key[i % 4].
//   - involution: mask(mask(x)) = x  (same key)
//   - length preservation.
#ifndef WS_CORE_MASK_H
#define WS_CORE_MASK_H

#include "ws/types.h"

// In-place mask/unmask of `len` bytes with the 4-byte `key`.
// Masking and unmasking are the same op (XOR involution).
void ws_mask(u8 *data, size_t len, const u8 key[4]);

#endif // WS_CORE_MASK_H
