# WebSocket (RFC6455) Protocol Stack — C23 / freestanding

## アーキテクチャ決定 (確定)
- 言語: C23 (`-std=c23`/`-std=c2x`), freestanding (`-ffreestanding -nostdlib`)
- ターゲット: x86-64 Linux。syscall は inline asm 直叩き (read/write/socket/...)
- レイヤード:
  - `platform/`  : syscall ラッパ, freestanding な memcpy/memset, エントリ
  - `core/`      : frame parse/build, masking, payload-len codec, UTF-8 検証, close codes
  - `protocol/`  : handshake (HTTP upgrade), 接続状態機械, fragmentation 集約
  - `sdk/`       : 公開 API (ws_*), I/O ループ
- 各層は下位層のみ依存。`include/ws/` に公開ヘッダ。

## ビルド/品質ゲート
- Nix flake (devShell): clang/lld, lean/lake, lizard, clang-tidy/format, just
- just: build / test / e2e / lint / cyclo / bench / proof / fmt / ci
- 複雑度: lizard (CCN しきい値で fail)
- lint: clang-tidy + clang-format --dry-run
- E2E: 実 TCP で本物の WS クライアントと対話
- bench: ローカルでフレーム処理スループット計測 → 自己改善ループ

## 形式検証 (実装の前に証明) — Lean 4
- [x] P1: masking XOR involution (mask(mask(x))=x), 長さ保存
- [x] P2: payload length codec roundtrip (decode∘encode = id, 3 形式) + 単射性
- [x] P3: UTF-8 validator 健全性 + 完全性 (受理 ⇔ 整形式)
- [x] 証明済み述語 → C テストへ橋渡し (test-first)。全 sorry なし・sorryAx 非依存。

## 実装フェーズ (各 test-first)
- [x] F0: 足場 (flake.nix, justfile, .clang-* , freestanding ビルド)
- [x] F1: platform 層 (syscall, mem*) + unit test
- [x] F2: core/masking (P1) test-first
- [x] F3: core/frame codec (P2) test-first
- [x] F4: core/utf8 (P3) test-first
- [x] F5: protocol/handshake (SHA1 + base64 accept key) test-first
- [x] F6: protocol/state machine + fragmentation (conn.c)
- [x] F7: sdk 公開 API (ws.h) + freestanding server (_start)
- [x] F8: E2E ハーネス (stdlib のみ Python クライアント)
- [x] F9: bench + 自己改善 (mask 18x)
- [x] F10: CI 集約 (just ci 緑)

## レビュー欄
- 形式検証を実装の前に完了し、証明済み定義をそのまま C テストのオラクルにした。
  proof と実装の乖離は test_mask/test_frame/test_utf8 が検出する。
- レイヤード: platform → core → protocol → sdk。各層は下位のみ依存、公開は include/ws/。
- libc 非依存を維持: freestanding 個別コンパイル成功、mask は __builtin_memcpy が
  MOV へ完全インライン化されリロケーション無し (nm で確認済み)。
- 品質ゲートは just ci に集約: proof / lint(tidy=error) / cyclo(CCN<=10) / build /
  unit / e2e(実TCP) / bench。全て緑。
- 自己改善ループの実証: bench で計測 → mask を 64bit ワード化 → 約18倍、性質は不変。
- 既知の簡略化を解消 (旧 ponytail メモ):
  - client 送信マスク鍵: 固定鍵 → sys_getrandom 由来の CSPRNG 鍵 (RFC6455 §5.3)。
    getrandom 失敗時は予測可能鍵で送らず送信失敗を選ぶ。test_client_mask_key_random。
  - close 即時応答 → closing handshake の往復化 (S-03/S-05)。close_sent で
    高々1回送信・冪等。CLOSING で送れば handshake 完了 → CLOSED。
  - デモサーバ単一接続・逐次 → epoll 多重化 (MAX_CONN=64、接続ごとに ws_conn と
    集約バッファ)。E2E で同時8接続・交互送受信を検証。
- 残る ponytail 簡略化: デモの集約上限は 128KiB (SDK 既定は 1MiB)。接続テーブルは
  固定長配列 (動的確保なし=freestanding の制約)。いずれもデモ専用で SDK コアに無影響。
