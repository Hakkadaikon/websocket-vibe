// ws — freestanding base types (C23, no libc).
#ifndef WS_TYPES_H
#define WS_TYPES_H

// Freestanding C23 provides these headers without libc.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;

#endif // WS_TYPES_H
