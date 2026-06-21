// ws core — フレームヘッダのパース/ビルド (RFC6455 §5.2)。
// 長さコーデックは Lean WsProof.LengthCodec で証明済み:
//   encode: <=125 -> 1 バイト; 126 -> +2 BE; 127 -> +8 BE。
//   decode(encode n) = n  (ラウンドトリップ)、encode は単射 (正準/最小)。
#ifndef WS_CORE_FRAME_H
#define WS_CORE_FRAME_H

#include "ws/frame.h"
#include "ws/types.h"

// `buf` (利用可能なバイト数 `len`) からフレームヘッダをパースする。
// WS_PARSE_OK のとき `out` が埋められ、out->header_len バイトが消費される。
// バイトが足りなければ NEED_MORE、プロトコル違反 (非最小な長さ符号化、または
// payload_len の最上位ビットが立っている) のとき ERROR を返す。
ws_parse_status ws_frame_parse_header(const u8 *buf, size_t len, ws_frame_header *out);

// フレームヘッダを `buf` (容量 `cap`) にシリアライズする。ヘッダのバイト数を返す。
// `cap` が小さすぎる場合は 0。masked==false のとき `mask_key` は NULL でよい。
// 常に最小 (正準) の長さ形式で出力する。
size_t ws_frame_build_header(u8 *buf, size_t cap, bool fin, u8 opcode, bool masked,
                             const u8 mask_key[4], u64 payload_len);

#endif // WS_CORE_FRAME_H のガード終端
