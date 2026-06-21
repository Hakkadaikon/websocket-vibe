# websocket-vibe — Claude 向けプロジェクト規律

C23 / freestanding(libc 非依存)の RFC6455 WebSocket プロトコルスタック。
形式検証ファーストで、Lean 4 の証明済み定義を C 実装の正準仕様にする。

## 守ること(提出前に全部緑)

- **freestanding を壊さない**: `src/` は `-ffreestanding -nostdlib -fno-builtin` でビルドする。libc も標準ヘッダ(stdio.h 等)も使わない。必要な機能は `src/platform/` の syscall ラッパ・`ws_mem*`・`__builtin_*` で賄う。デモサーバ `ws_server` は libc にリンクしてはならない(`just verify-freestanding` が検査する)。
- **形式検証ファースト**: プロトコル仕様は先に Lean 4(`proof/`)で証明し、証明済み述語を C テストのオラクルにする。実装を証明より先に書かない。`sorry` を残したまま「保証済み」と言わない。
- **test-first**: 振る舞いの変更はテストを先に書く。挙動の不変を主張するなら、それを落とせるテストで示す。
- **循環的複雑度 CCN ≤ 3**: 全関数。超えたら補助関数へ MECE に分割する(`just cyclo`)。
- **レイヤード依存**: `platform → core → protocol → sdk`。各層は下位層だけに依存する。公開ヘッダは `include/ws/` のみ。
- **sans-IO コア**: `src/sdk/conn.c` は syscall を呼ばない。I/O は呼び出し側/サーバループの責務。

## ビルドと検証

- `just ci` が統合ゲート(proof / lint / cyclo / build / verify-freestanding / test / sanitize / e2e / bench)。提出は ci 緑が条件。
- 個別: `just proof`(Lean)、`just test`(unit)、`just sanitize`(ASan/UBSan/LSan で unit を再実行。コアをホワイトボックス include するのでメモリ安全・UB・リークを検出)、`just e2e`(実 TCP)、`just lint`(clang-format + clang-tidy、findings はエラー)、`just verify-freestanding`(libc 非リンク検査)。
- 一時ファイルは `$TMPDIR`(`/tmp` は書込不可な環境がある)。

## 詳細

- 開発手順・形式検証ワークフロー: `docs/development.md`
- セキュリティ設計・TLS 未対応の注意: `docs/security.md`
- 証明した 28 性質と定理の対応: `proof/SPEC_PROPERTIES.md`
