# 開発ガイド

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

## 開発環境

開発環境は Nix flake（`flake.nix`）で固定する。
clang 19、lld、lizard、clang-tools、just、python3 を pin する。
Lean は dotfiles の toolchain を使う。

## 形式検証ワークフロー

実装の前に Lean 4 で仕様を証明し、証明済みの述語をそのまま C ユニットテストのオラクルにする（test-first）。
証明された定義が C 実装の正準仕様である。
証明はすべて sorry なしで、`#print axioms` により sorryAx に依存しないことを確認している。

数学的性質として、次の3つを `proof/` で証明する。

- **Masking**（WsProof.Masking）：`out[i] = data[i] XOR key[i % 4]`。XOR involution（2回マスクすると元に戻る）と長さ保存を証明する。
- **Length codec**（WsProof.LengthCodec）：ペイロード長 encode/decode の roundtrip と単射性（最小形）を証明する。≤125 は1バイト、126 は +2バイトの big-endian、127 は +8バイトの big-endian で表す。
- **UTF-8**（WsProof.Utf8）：バリデータの健全性と完全性、すなわち受理が RFC3629 の整形式と同値であることを証明する。

これらに加えて、状態機械、フレーミング、制御フレーム、マスク・エラー処理にわたるプロトコル仕様 28 性質を `WsProof.Spec.*` で証明している。
各性質の定理名と C 実装の分岐との対応は [proof/SPEC_PROPERTIES.md](../proof/SPEC_PROPERTIES.md) にまとめてある。

## 性能と自己改善ループ

bench で計測し、改善し、再計測するループを回す。
マスキングを byte 単位から 64bit ワード単位の XOR に最適化し、大ペイロードで約18倍の改善を得た（約 1.4 GiB/s から約 25 GiB/s）。
証明済みの XOR involution 仕様は最適化を通じて不変であり、`test_mask` がこれをガードする。

## 貢献の流れ

- test-first を守る。仕様を Lean で証明してから、証明済み述語をテストにして実装する。
- `just proof` が緑であり、`just ci` が通ることを提出条件にする。
- 変更は論理単位（おおむね 30〜50 行）に分けて conventional commit でコミットする。
