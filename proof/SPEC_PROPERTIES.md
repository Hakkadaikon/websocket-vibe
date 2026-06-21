# RFC6455 仕様性質カタログ(形式検証対象の洗い出し)

出典: RFC6455 本文 https://datatracker.ietf.org/doc/html/rfc6455 (normative 文言を裏取り済み)。
4観点(状態機械 / フレーミング / 制御・タイミング / マスク・エラー)で棚卸しし、重複を統合した。
数学的性質(masking involution, length roundtrip, UTF-8 健全性)は既証(`WsProof.{Masking,LengthCodec,Utf8}`)。
本カタログは **プロトコル仕様としての性質**(状態機械の不変条件・MUST/MUST NOT・ワークフロー・順序/タイミング)。

凡例: 型 = invariant(単一ステップ不変) / pre / post / temporal(trace上の順序・応答義務・到達可能性)。
優先度 = C(critical) / I(important) / N(nice-to-have)。

## A. 状態機械ライフサイクル (§5.1, §7)
| ID | 性質 | RFC | 型 | 優先 |
|----|------|-----|----|----|
| S-01 | 状態は OPEN→CLOSING→CLOSED の一方向遷移のみ(逆行しない) | §7.1.2-4 | invariant | C |
| S-02 | CLOSED は吸収状態(以降どのイベントでも CLOSED のまま) | §7.1.4 | invariant | C |
| S-03 | Close 送信後はデータフレームを送らない("does not send any further data") | §5.5.1 | invariant | C |
| S-04 | Close 受信後は以降のデータを破棄する("discards any further data") | §5.5.1 | invariant | I |
| S-05 | Close 受信→未送信なら Close で応答(送信済みなら不要) | §5.5.1 | post | C |
| S-06 | 両端同時 Close(race)でも双方 CLOSED に収束 | §7.1.2 | temporal | I |

## B. フレーミング / フラグメント (§5.2, §5.4)
| ID | 性質 | RFC | 型 | 優先 |
|----|------|-----|----|----|
| F-01 | 単一メッセージ = FIN=1 ∧ opcode≠0 | §5.4 | invariant | C |
| F-02 | フラグメント列 = [FIN=0,op≠0] ++ [FIN=0,op=0]* ++ [FIN=1,op=0] | §5.4 | invariant | C |
| F-03 | continuation(op=0)は進行中メッセージがある時のみ許容(文脈外は fail) | §5.4 | pre | C |
| F-04 | 進行中メッセージ中に新規データフレーム(op≠0)開始は禁止(interleave 禁止) | §5.4 | invariant | C |
| F-05 | 集約メッセージのバイト列 = 各フラグメント payload の順序保存連結 | §5.4 | post | C |
| F-06 | メッセージ全体の opcode = 最初のフラグメントの opcode(text/binary) | §5.4 | invariant | C |
| F-07 | RSV1/2/3 が非0(拡張未交渉)なら fail | §5.2 | pre | C |
| F-08 | 未知 opcode は fail | §5.2 | pre | C |
| F-09 | 非最小長エンコードは fail(既証 LengthCodec の単射性が根拠) | §5.2 | pre | C |
| F-10 | 64bit 長の最上位ビットが1なら fail | §5.2 | pre | I |
| F-11 | N メッセージ送信 → N メッセージを順序通り受信(境界保存) | §6 | temporal | I |

## C. 制御フレーム / タイミング (§5.4, §5.5)
| ID | 性質 | RFC | 型 | 優先 |
|----|------|-----|----|----|
| C-01 | 制御フレームがフラグメント途中に挟まっても集約状態(msg_len,in_message)を壊さない | §5.4 | invariant | C |
| C-02 | 制御フレームは payload ≤125(超過は fail) | §5.5 | pre | C |
| C-03 | 制御フレームは fragment 不可(FIN=1 必須、FIN=0 は fail) | §5.5 | pre | C |
| C-04 | Ping 受信→Pong 応答義務(Close 受信済みを除く) | §5.5.2 | temporal | I(層注) |
| C-05 | Pong の payload = 元 Ping の payload | §5.5.2 | post | C |
| C-06 | unsolicited Pong を受理してよい(状態を壊さない) | §5.5.3 | invariant | N |
| C-07 | 受信粒度非依存: フレームを任意チャンク分割で供給しても同一結果 | §6(実装) | temporal | I |

## D. マスク / エラー / close code (§5.1, §7.4, §8.1)
| ID | 性質 | RFC | 型 | 優先 |
|----|------|-----|----|----|
| M-01 | server は masked クライアントフレームのみ受理 | §5.1 | invariant | C |
| M-02 | unmasked クライアントフレーム受信→必ず fail(close) | §5.1 | post | C |
| M-03 | server 送信フレームは必ず unmasked | §5.1 | invariant | C |
| M-04 | 全フレーム受信は accept か fail のどちらかに決定的に分類される(網羅性) | §5/§7 | invariant | C |
| M-05 | エラー検出→close 送信→CLOSED の一方向(Fail the Connection) | §7.1.7 | temporal | C |
| M-06 | close code 1005/1006/1015 をフレームに出さない | §7.4.1 | invariant | I |
| M-07 | 不正 close code 受信時の扱い(1002 で fail) | §7.4.1 | post | I |
| M-08 | text/close reason が不正 UTF-8 なら fail(1007)。既証 Utf8 が判定の核 | §8.1 | post | C |

## 形式化方針(提案)
- **抽象状態機械を Lean で構築**: `WsState`(open/closing/closed)、`Event`(data/continuation/control/close/error)、
  決定的 step 関数 `step : WsState → Frame → (WsState × Action)` を定義。
  A/D の invariant(S-01..03, M-01..05)は step の単一ステップ性質として証明。
- **フラグメント集約を純関数で**: `aggregate : List Frame → Result (List Message)` を定義し、
  B(F-01..F-06)と C-01 を証明。F-05 連結は `List.flatten`/`++` で表現。
- **temporal 性質(順序・応答義務)は trace 述語**: `valid_trace : List Event → Prop`。
  C-04/C-07/F-11/S-06 はここ。応答義務 C-04 は「層注」: SDK は ping を *event* として通知する所まで
  保証し、pong 送信はアプリ責務。よって「ping イベントが payload 込みで正しく通知される」を証明対象とする。
- **既証3定理を再利用**: F-09 は LengthCodec 単射性、M-08 は Utf8 健全性、C-05 は Masking で payload 不変。

## 証明状況(28要素 → Lean 定理の対応)

全 28 要素を `WsProof.Spec.*` で証明済み(`lake build` 緑・`sorryAx` 非依存)。

| ID | 定理 | ファイル |
|----|------|----------|
| S-01 | `step_monotone` | StateMachine |
| S-02 | `step_closed_absorbing` | StateMachine |
| S-03 | `no_data_after_close_sent` | Workflow |
| S-04 | `data_discarded_after_close_recv` | Workflow |
| S-05 | `close_from_open` / `close_from_closing` | StateMachine |
| S-06 | `close_race_converges` | Trace |
| F-01 | `WellFormedMsg.single` / `aggregate_opcode` | Fragment |
| F-02 | `WellFormedMsg.multi` / `WellFormedTail` | Fragment |
| F-03 | `continuation_needs_context` | Fragment |
| F-04 | `no_interleave` | Fragment |
| F-05 | `aggregate_payload_concat` | Fragment |
| F-06 | `aggregate_opcode` | Fragment |
| F-07 | `rsv_nonzero_rejected` | StateMachine |
| F-08 | `unknown_opcode_rejected` | StateMachine |
| F-09 | `f09_minimal_length_basis`(既証 `encode_injective` 橋渡し) | Workflow |
| F-10 | `msb_set_len_rejected` | Workflow |
| F-11 | `roundtrip_messages` | Trace |
| C-01 | `control_preserves_acc` / `feed_filters_control` | Fragment |
| C-02 | `control_oversize_rejected` | StateMachine |
| C-03 | `control_fragmented_rejected` | StateMachine |
| C-04 | `ping_notified_with_payload`(層注) | Trace |
| C-05 | `pong_echoes_ping_payload` / `pong_payload_from_ping_event` | Workflow |
| C-06 | `unsolicited_pong_preserves_state` | Workflow |
| C-07 | `chunking_invariant` / `split_join` | Trace |
| M-01 | `server_accepts_masked` | Workflow |
| M-02 | `server_rejects_unmasked` | StateMachine |
| M-03 | `server_frames_unmasked` | Workflow |
| M-04 | `error_implies_closed` | StateMachine |
| M-05 | `error_implies_closed`(Fail→closed の一方向) | StateMachine |
| M-06 | `sanitized_close_code_not_reserved` | Workflow |
| M-07 | `invalid_close_code_yields_1002` | Workflow |
| M-08 | `m08_utf8_decision_basis`(既証 `validate_correct` 橋渡し) | Workflow |

## C 実装との対応
証明した step 関数/述語を、`conn.c` の `consume_frame`/`header_ok`/`accept_data`/`accept_control` の
分岐構造に1対1で対応させ、その対応を C ユニットテスト(`test_conn.c` 拡張)で test-first に確認する。
これにより proof と実装の乖離をテストが検出する(数学性質3定理と同じ橋渡し方式)。
