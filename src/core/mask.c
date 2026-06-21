#include "core/mask.h"

void ws_mask(u8 *data, size_t len, const u8 key[4]) {
    for (size_t i = 0; i < len; i++)
        data[i] ^= key[i & 3u];
}
