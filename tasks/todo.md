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
- [ ] P1: masking XOR involution (mask(mask(x))=x), 長さ保存
- [ ] P2: payload length codec roundtrip (decode∘encode = id, 3 形式)
- [ ] P3: UTF-8 validator 健全性 (受理 ⇔ 整形式)
- 証明済み述語 → C テストへ橋渡し (test-first)

## 実装フェーズ (各 test-first)
- [ ] F0: 足場 (flake.nix, justfile, .clang-* , 最小 freestanding ビルド通す)
- [ ] F1: platform 層 (syscall, mem*) + unit test
- [ ] F2: core/masking (P1) test-first
- [ ] F3: core/frame codec (P2) test-first
- [ ] F4: core/utf8 (P3) test-first
- [ ] F5: protocol/handshake (SHA1 + base64 accept key) test-first
- [ ] F6: protocol/state machine + fragmentation
- [ ] F7: sdk 公開 API + I/O ループ
- [ ] F8: E2E ハーネス
- [ ] F9: bench + 自己改善
- [ ] F10: CI 集約 (just ci)

## レビュー欄
(完了後に記入)
