---
paths:
  - "src/**/*.c"
  - "src/**/*.h"
  - "include/**/*.h"
---

# freestanding C のルール(src/ と公開ヘッダ)

このリポジトリの `src/` は libc 非依存・freestanding でビルドする
(`-std=c2x -ffreestanding -nostdlib -fno-builtin -Wall -Wextra -Werror -Wpedantic`)。
ここを編集するとき必ず守る。

- **libc を使わない**: malloc/printf/memcpy 等の libc 関数も、stdio.h などの標準ヘッダも禁止。
  メモリ操作は `src/platform/mem.h` の `ws_memcpy`/`ws_memset`/`ws_memcmp`、
  OS 機能は `src/platform/sys.h` の syscall ラッパを使う。インライン展開には `__builtin_*` 可。
- **新規 syscall は `src/platform/sys.c` に**: inline asm の番号直叩きで足し、`sys.h` に宣言する。
- **CCN ≤ 3**: 追加・変更した関数は循環的複雑度 3 以下。超えるなら補助述語/段階関数へ MECE に分割する。
  `&&`/`||`/三項/早期 return も分岐として数えられる(`just cyclo` = `lizard src -C 3 -w`)。
- **レイヤード依存を守る**: `platform → core → protocol → sdk`。上位を下位から include しない。
  公開 API は `include/ws/` のヘッダだけ。内部ヘッダ(`src/*/`)を公開境界に漏らさない。
- **sans-IO を守る**: `src/sdk/conn.c` は syscall を呼ばない純粋な状態機械。I/O は呼び出し側の責務。
- **clang-format / clang-tidy = エラー**: 編集後 `clang-format -i` を当て、`just lint` を通す。
  特に narrowing(int→char 等)は clang-tidy で落ちるので注意。
- 一時ファイルは `$TMPDIR` を使う(`/tmp` が書込不可な環境がある)。

提出前に少なくとも `just lint`・`just cyclo`・`just verify-freestanding`、
変更層に対応する `just test` または `just e2e` を緑にする。
