---
paths:
  - "proof/**/*.lean"
  - "tests/**"
---

# 形式検証と proof↔実装の橋渡し

このリポジトリは形式検証ファースト。プロトコル仕様は先に Lean 4(`proof/`)で証明し、
証明済み述語を C テストのオラクルにする。`proof/` や `tests/` を触るとき守る。

## proof/(Lean 4)

- `just proof`(= `cd proof && lake build`)が緑であること。エラーは証明の穴。
- **`sorry` を残したまま「保証済み」と言わない**。残すなら「未証明」と明示する。
  証明が標準公理だけに依存することは `#print axioms <name>` で確認できる(`sorryAx` 非依存)。
- 検証対象を数学的性質だけにしない。状態機械の不変条件・MUST/MUST NOT・
  temporal(順序・応答義務・収束)も対象。新性質は `proof/SPEC_PROPERTIES.md` の
  カタログに ID・性質・出典・対応定理を追記する。
- 既証の数学的補題(masking involution / length codec 単射性 / UTF-8 健全性)は、
  仕様性質の「根拠」として橋渡し定理で再利用する。

## tests/(proof↔実装の乖離検出)

- **証明したのはモデルで実装そのものではない**。証明した step 関数/述語の分岐を、
  C 実装の対応分岐に1対1で対応させ、その対応をテストで固定する。
- 振る舞いを変える/モデルに合わせるときは **test-first**: テストを先に書いて落とし、
  実装をモデルへ寄せて緑にする。挙動不変を主張するなら落とせるテストで示す。
- unit テストは実装をホワイトボックスで `#include` する(`tests/unit/test_*.c`)。
  E2E は実 TCP で freestanding サーバを駆動する(`tests/e2e/`、stdlib のみ)。
- 一時ファイルは `$TMPDIR`。

提出前に `just proof` と、対応する `just test` / `just e2e` を緑にする。
