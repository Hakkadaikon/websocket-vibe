# websocket-vibe

C23 で書いた RFC6455 WebSocket プロトコルスタック（SDK）である。
libc に依存しない freestanding 実装で、`-std=c2x -ffreestanding -nostdlib -fno-builtin` でビルドする。

ターゲットは x86-64 Linux である。
read/write/socket/bind/listen/accept/close/setsockopt/getrandom/epoll_create1/epoll_ctl/epoll_wait/exit などの syscall は inline asm で直接発行し、ランタイムに何も要求しない。

開発は test-first で進める。
各プロトコル仕様を Lean 4 で証明してから、その証明済みの定義を正準仕様として C 実装を書く。

## アーキテクチャ

レイヤード構成で、各層は下位層だけに依存する。

`src/platform/` は freestanding な土台を提供する。
メモリ操作（`ws_memcpy` / `ws_memset` / `ws_memcmp`）と、x86-64 Linux syscall のラッパを置く。

`src/core/` は RFC6455 §5 のフレーム処理を担う。
マスキング（mask.c）、フレームヘッダの codec（frame.c）、UTF-8 検証（utf8.c）からなる。

`src/protocol/` は RFC6455 §4 のハンドシェイクを担う。
SHA-1（sha1.c）、base64（base64.c）、accept key の計算と HTTP パース（handshake.c）からなる。

`src/sdk/` は公開 API を提供する。
sans-IO の接続状態機械（conn.c）がフラグメント集約、制御フレーム、close を扱い、freestanding なデモエコーサーバ（server.c）が独自の `_start` からこれを駆動する。
デモサーバは epoll で readiness を多重化し、固定接続テーブル（最大64接続）で複数クライアントを1スレッドで同時に捌く。

公開ヘッダは `include/ws/` に置く（types.h、frame.h、ws.h）。

## 形式検証

実装の前に、次の3性質を Lean 4 で証明する（`proof/`）。
いずれも sorry なしで証明済みで、`#print axioms` により sorryAx に依存しないことを確認している。
証明された定義が C 実装の正準仕様であり、証明した述語はそのまま C ユニットテストのオラクルになる。

- **Masking**（WsProof.Masking）：`out[i] = data[i] XOR key[i % 4]`。XOR involution（2回マスクすると元に戻る）と長さ保存を証明する。client フレームのマスク鍵は getrandom 由来の CSPRNG から毎フレーム取る（RFC6455 §5.3）。
- **Length codec**（WsProof.LengthCodec）：ペイロード長 encode/decode の roundtrip と単射性（最小形）を証明する。≤125 は1バイト、126 は +2バイトの big-endian、127 は +8バイトの big-endian で表す。
- **UTF-8**（WsProof.Utf8）：バリデータの健全性と完全性、すなわち受理が RFC3629 の整形式と同値であることを証明する。

## ビルドと品質ゲート

タスクは just で定義する。

- `just build`：freestanding 静的アーカイブ `build/libws.a` とデモサーバ `build/ws_server` をビルドする。
- `just test`：各層のユニットテストを実行する（ホスト toolchain でコンパイルし、white-box で include する）。
- `just e2e`：実 TCP で、stdlib のみの Python WS クライアントが freestanding サーバを駆動する（echo、ping、fragment、100KB、close、不正 UTF-8 の拒否、並行接続）。
- `just cyclo`：lizard で循環的複雑度を計測し、CCN が 10 を超えたら失敗する。
- `just lint`：clang-format のチェックと clang-tidy を実行する（findings はエラー扱い）。
- `just bench`：フレーム parse と unmask のローカルスループットを計測する（ns/op、MiB/s）。
- `just proof`：Lean 4 で形式検証を実行する（`lake build`）。
- `just ci`：上記をすべて通す統合ゲートである。

開発環境は Nix flake（`flake.nix`）で固定する。
clang 19、lld、lizard、clang-tools、just、python3 を pin する。
Lean は dotfiles の toolchain を使う。

## 性能

bench で計測し、改善し、再計測するループを回す。
マスキングを byte 単位から 64bit ワード単位の XOR に最適化し、大ペイロードで約18倍の改善を得た（約 1.4 GiB/s から約 25 GiB/s）。
証明済みの XOR involution 仕様は最適化を通じて不変であり、test_mask がこれをガードする。

## 使い方

sans-IO コアの典型フローを示す（ws.h）。
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

`ws_send_ping` / `ws_send_pong` / `ws_send_close` も同じく出力フレームを `out` に構築し、書き込んだバイト数を返す。
`ws_send_close` は Close を高々1回だけ送る（冪等）。CLOSING で送れば往復が揃って CLOSED になり、それ以外では CLOSING に入る。close は状態機械の中で完結する。
