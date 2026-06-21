# WebSocket (RFC6455) Protocol Stack — C23 / freestanding

全フェーズ完了。アーキテクチャ・ビルドゲート・公開 API は README.md、
証明した28性質と定理の対応は proof/SPEC_PROPERTIES.md を参照。

## 完了サマリ
- 形式検証 (実装の前に証明): P1 masking involution / P2 length codec roundtrip+単射性 /
  P3 UTF-8 健全性+完全性。プロトコル28性質も Lean で証明済み。全 sorry なし・sorryAx 非依存。
- 実装 F0-F10: 足場 → platform → core(mask/frame/utf8) → protocol(handshake) →
  sdk(conn/server) → E2E → bench → CI。各 test-first。

## レビュー欄
- 形式検証を実装の前に完了し、証明済み定義をそのまま C テストのオラクルにした。
  proof と実装の乖離は test_mask/test_frame/test_utf8/test_conn が検出する。
- libc 非依存を維持: freestanding 個別コンパイル成功、mask は __builtin_memcpy が
  MOV へ完全インライン化されリロケーション無し (nm で確認済み)。
- 自己改善ループの実証: bench で計測 → mask を 64bit ワード化 → 約18倍、性質は不変。
- 既知の簡略化を解消:
  - client 送信マスク鍵: 固定鍵 → sys_getrandom 由来の CSPRNG 鍵 (RFC6455 §5.3)。
    getrandom 失敗時は予測可能鍵で送らず送信失敗を選ぶ。test_client_mask_key_random。
  - close 即時応答 → closing handshake の往復化 (S-03/S-05)。close_sent で
    高々1回送信・冪等。CLOSING で送れば handshake 完了 → CLOSED。
  - デモサーバ単一接続・逐次 → epoll 多重化 (MAX_CONN=64、接続ごとに ws_conn と
    集約バッファ)。E2E で同時8接続・交互送受信を検証。
- 残る ponytail 簡略化: デモの集約上限は 128KiB (SDK 既定は 1MiB)。接続テーブルは
  固定長配列 (動的確保なし=freestanding の制約)。いずれもデモ専用で SDK コアに無影響。
