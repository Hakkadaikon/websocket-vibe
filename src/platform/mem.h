// ws platform — freestanding memory primitives (no libc).
#ifndef WS_PLATFORM_MEM_H
#define WS_PLATFORM_MEM_H

#include "ws/types.h"

void *ws_memcpy(void *dst, const void *src, size_t n);
void *ws_memset(void *dst, int c, size_t n);
int   ws_memcmp(const void *a, const void *b, size_t n);

#endif // WS_PLATFORM_MEM_H
