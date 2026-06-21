# websocket-vibe

C23 で書いた RFC6455 WebSocket プロトコルスタック（SDK）である。
libc に依存しない freestanding 実装で、x86-64 Linux の syscall を inline asm で直接発行し、ランタイムに何も要求しない。
I/O は sans-IO コアに閉じ込め、付属のデモサーバが epoll でこれを駆動する。

## ディレクトリ構造

```
src/platform/   freestanding な土台（メモリ操作、x86-64 Linux syscall ラッパ）
src/core/       RFC6455 §5 のフレーム処理（マスキング、ヘッダ codec、UTF-8 検証）
src/protocol/   RFC6455 §4 のハンドシェイク（SHA-1、base64、accept key、HTTP パース）
src/sdk/        公開 API。sans-IO 接続状態機械と freestanding デモエコーサーバ
include/ws/     公開ヘッダ（types.h、frame.h、ws.h）
proof/          Lean 4 の形式検証（数学的性質とプロトコル仕様）
tests/          ユニットテスト（unit/）と Python による E2E テスト（e2e/）
examples/       SDK の利用サンプル（echo/ はテキストを echo back する）
docs/           開発ガイド（development.md）とセキュリティ（security.md）
```

## 使い方

sans-IO コアの典型フローを示す。
ストレージはすべて呼び出し側が提供し、内部で malloc を使わない。

```c
#include "ws/ws.h"

ws_conn c;
u8 msg_buf[WS_MAX_MESSAGE];
ws_conn_init(&c, WS_ROLE_SERVER, msg_buf, sizeof msg_buf);

// 受信したバイトをコアに供給する。
ws_conn_recv(&c, rx, rx_len);

// イベントを WS_EV_NONE になるまで drain する。
ws_event ev;
while (ws_conn_poll(&c, &ev) != WS_EV_NONE) {
    switch (ev.type) {
    case WS_EV_MESSAGE: /* ev.data / ev.len を処理 */ break;
    case WS_EV_PING:    /* ws_send_pong で応答 */ break;
    case WS_EV_CLOSE:   /* ev.close_code を読み、ws_send_close */ break;
    case WS_EV_ERROR:   /* プロトコル違反、接続を閉じる */ break;
    default: break;
    }
}

// 出力フレームを呼び出し側バッファに構築する。
u8 out[4096];
size_t n = ws_send_message(&c, WS_OP_TEXT, payload, payload_len, out, sizeof out);
// n バイトを送信する（n == 0 は失敗）。
```

`ws_send_ping` / `ws_send_pong` / `ws_send_close` も出力フレームを `out` に構築し、書き込んだバイト数を返す。

## RFC6455 実装状況

| 機能 | 状況 |
|------|------|
| §4 Opening Handshake（Upgrade/Key→Accept、SHA-1+base64） | 実装済み（server ロール） |
| §4 サブプロトコル交渉（Sec-WebSocket-Protocol） | 未実装 |
| §4 拡張交渉（Sec-WebSocket-Extensions / permessage-deflate） | 未実装 |
| §5.2 フレーミング（FIN/RSV/opcode/mask、長さ 7/16/64bit） | 実装済み |
| §5.3 client→server マスキング | 実装済み |
| §5.4 フラグメント集約 | 実装済み |
| §5.5 制御フレーム（close/ping/pong） | 実装済み |
| §5.5.1 closing handshake / close code | 実装済み |
| §6 データ送受信（text/binary、UTF-8 検証） | 実装済み |
| §7.4.1 close code の扱い（許容域、1005/1006/1015） | 実装済み |
| §8 Fail the Connection | 実装済み |
| client ロールの opening handshake 生成 | 未実装（フレーム送受信は client 可、接続確立は server のみ） |
| TLS / wss://（§3） | 未実装（平文のみ） |
| I/O モデル | sans-IO コア + epoll 多重化デモサーバ（最大64同時接続） |

## セキュリティ

マスク鍵の扱い、入力検証、TLS 未対応の注意などセキュリティ上の設計は [docs/security.md](docs/security.md) を参照する。

## ライセンス

MIT License。詳細は [LICENSE](LICENSE) を参照する。
