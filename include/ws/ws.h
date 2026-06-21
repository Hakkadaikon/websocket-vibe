// ws — 公開 SDK (RFC6455)。sans-IO コア: バイトを流し込み、バイトを汲み出す。
// この層に syscall はないため、フリースタンディングで動き、完全にテスト可能。
// 別のサーバループ (src/sdk/server.c) がこれをソケットに接続する。
#ifndef WS_WS_H
#define WS_WS_H

#include "ws/frame.h"
#include "ws/types.h"

#define WS_MAX_MESSAGE (1u << 20) // 集約の既定上限 1 MiB

typedef enum {
    WS_ROLE_SERVER = 0, // クライアントのマスク済みフレームを期待し、自身は非マスクで送る
    WS_ROLE_CLIENT = 1, // マスク済みフレームを送り、相手には非マスクを期待する
} ws_role;

typedef enum {
    WS_ST_OPEN = 0,
    WS_ST_CLOSING = 1,
    WS_ST_CLOSED = 2,
} ws_conn_state;

// ws_conn_recv() の後に呼び出し側へ通知されるイベント。
typedef enum {
    WS_EV_NONE = 0,    // バイトが足りない
    WS_EV_MESSAGE = 1, // 完全なデータメッセージが揃った (text/binary)
    WS_EV_PING = 2,
    WS_EV_PONG = 3,
    WS_EV_CLOSE = 4, // 相手がクローズを開始した
    WS_EV_ERROR = 5, // プロトコル違反。接続を閉じる必要がある
} ws_event_type;

typedef struct {
    ws_event_type type;
    u8 opcode;      // MESSAGE の場合は WS_OP_TEXT / WS_OP_BINARY
    const u8 *data; // ペイロード (conn のメッセージバッファ内を指す)
    size_t len;
    u16 close_code; // WS_EV_CLOSE 用
} ws_event;

// 不透明なコネクション。ストレージは呼び出し側が用意する (フリースタンディングに malloc はない)。
// WS_CONN_SIZE バイト、WS_CONN_ALIGN のアライメントを使う。
#define WS_CONN_SIZE  256
#define WS_CONN_ALIGN 8
typedef struct {
    _Alignas(WS_CONN_ALIGN) u8 _opaque[WS_CONN_SIZE];
} ws_conn;

// --- ライフサイクル ---
// `c` をその場で初期化する。`msg_buf`/`msg_cap` はメッセージ集約バッファの裏付けで
// (コネクションより長生きする必要がある)。ストレージが小さすぎれば false を返す。
bool ws_conn_init(ws_conn *c, ws_role role, u8 *msg_buf, size_t msg_cap);
ws_conn_state ws_conn_status(const ws_conn *c);

// --- 受信: 受け取ったバイトを流し込み、イベントをドレインする ---
// 最大 `len` バイトを流し込み、消費したバイト数を返す。流し込んだバイトから
// 完成した全イベントをドレインするには、WS_EV_NONE になるまで ws_conn_poll() を呼ぶ。
size_t ws_conn_recv(ws_conn *c, const u8 *buf, size_t len);
ws_event_type ws_conn_poll(ws_conn *c, ws_event *ev);

// --- 送信: 呼び出し側バッファにフレームを組み立て、書き込んだバイト数を返す (0=失敗) ---
size_t ws_send_message(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out, size_t cap);
size_t ws_send_ping(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap);
size_t ws_send_pong(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap);
size_t ws_send_close(ws_conn *c, u16 code, u8 *out, size_t cap);

#endif // WS_WS_H のガード終端
