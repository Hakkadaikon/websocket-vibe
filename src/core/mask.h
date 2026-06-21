// ws core — RFC6455 §5.3 ペイロードマスキング。
// 証明済み (Lean WsProof.Masking): transformed[i] = data[i] XOR key[i % 4]。
//   - 対合性: mask(mask(x)) = x  (同じキー)
//   - 長さ保存。
#ifndef WS_CORE_MASK_H
#define WS_CORE_MASK_H

#include "ws/types.h"

// 4 バイトの `key` で `len` バイトをその場でマスク/アンマスクする。
// マスクとアンマスクは同一の操作 (XOR の対合性)。
void ws_mask(u8 *data, size_t len, const u8 key[4]);

#endif // WS_CORE_MASK_H のガード終端
