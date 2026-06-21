// ws core — RFC3629 UTF-8 validation (RFC6455 §8.1 text frames).
// Branch structure proven in Lean WsProof.Utf8 (validate_correct:
// accept <-> WellFormed). Stateful variant allows validation across
// fragmented text frames.
#ifndef WS_CORE_UTF8_H
#define WS_CORE_UTF8_H

#include "ws/types.h"

// Incremental validator state. Zero-initialize to start.
typedef struct {
    u8 remaining; // continuation bytes still expected (0 = at boundary)
    u8 lo, hi;    // allowed range for the NEXT continuation byte
} ws_utf8_state;

// Feed `len` bytes. Returns false on the first ill-formed byte.
// State persists; a sequence may span calls.
bool ws_utf8_feed(ws_utf8_state *s, const u8 *buf, size_t len);

// True iff no partial sequence is pending (call after the final fragment).
bool ws_utf8_complete(const ws_utf8_state *s);

// One-shot whole-buffer validation (well-formed AND no trailing partial).
bool ws_utf8_valid(const u8 *buf, size_t len);

#endif // WS_CORE_UTF8_H
