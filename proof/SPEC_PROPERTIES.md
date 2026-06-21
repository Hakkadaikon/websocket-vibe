# RFC6455 仕様性質カタログ(形式検証対象)

出典: [RFC6455](https://datatracker.ietf.org/doc/html/rfc6455)(normative 文言を裏取り済み)。
状態機械 / フレーミング / 制御・タイミング / マスク・エラーの4観点で棚卸しした
**プロトコル仕様としての性質**(不変条件・MUST/MUST NOT・ワークフロー・順序/タイミング)。
数学的性質(masking involution, length roundtrip, UTF-8 健全性)は `WsProof.{Masking,LengthCodec,Utf8}` に既証。

全 28 要素を `WsProof.Spec.*` で証明済み(`lake build` 緑・`sorryAx` 非依存)。
フレーム検証性質は正準モデル `step`(状態機械)とワイヤパーサ `parseFrame`/`recvWire`(長さデコード)に
接続済みで、対応する C コードのバグは Lean の証明を落とす。各性質の接続経路は末尾「C 実装との対応」を参照。
型 = invariant(単一ステップ不変)/ pre / post / temporal(順序・応答義務・到達可能性)。
優先 = C(critical)/ I(important)/ N(nice-to-have)。

## A. 状態機械ライフサイクル (§5.1, §7)
| ID | 性質 | RFC | 型 | 優先 | 定理 (ファイル) |
|----|------|-----|----|----|----------------|
| S-01 | 状態は OPEN→CLOSING→CLOSED の一方向遷移のみ | §7.1.2-4 | invariant | C | `step_monotone` (StateMachine) |
| S-02 | CLOSED は吸収状態 | §7.1.4 | invariant | C | `step_closed_absorbing` (StateMachine) |
| S-03 | Close 送信後はデータフレームを送らない | §5.5.1 | invariant | C | `no_data_after_close_sent` (Workflow) |
| S-04 | Close 受信後は以降のデータを破棄する | §5.5.1 | invariant | I | `data_discarded_after_close_recv` (Workflow) |
| S-05 | Close 受信→未送信なら Close で応答 | §5.5.1 | post | C | `close_from_open` / `close_from_closing` (StateMachine) |
| S-06 | 両端同時 Close でも双方 CLOSED に収束 | §7.1.2 | temporal | I | `close_race_converges` (Trace) |

## B. フレーミング / フラグメント (§5.2, §5.4)
| ID | 性質 | RFC | 型 | 優先 | 定理 (ファイル) |
|----|------|-----|----|----|----------------|
| F-01 | 単一メッセージ = FIN=1 ∧ opcode≠0 | §5.4 | invariant | C | `WellFormedMsg.single` / `aggregate_opcode` (Fragment) |
| F-02 | フラグメント列 = [FIN=0,op≠0] ++ [FIN=0,op=0]* ++ [FIN=1,op=0] | §5.4 | invariant | C | `WellFormedMsg.multi` / `WellFormedTail` (Fragment) |
| F-03 | continuation は進行中メッセージがある時のみ許容 | §5.4 | pre | C | `continuation_needs_context` (Fragment) |
| F-04 | 進行中メッセージ中の新規データフレーム開始は禁止 | §5.4 | invariant | C | `no_interleave` (Fragment) |
| F-05 | 集約バイト列 = 各 payload の順序保存連結 | §5.4 | post | C | `aggregate_payload_concat` (Fragment) |
| F-06 | メッセージの opcode = 最初のフラグメントの opcode | §5.4 | invariant | C | `aggregate_opcode` (Fragment) |
| F-07 | RSV1/2/3 が非0(拡張未交渉)なら fail | §5.2 | pre | C | `rsv_nonzero_rejected` (StateMachine) |
| F-08 | 未知 opcode は fail | §5.2 | pre | C | `unknown_opcode_rejected` (StateMachine) |
| F-09 | 非最小長エンコードは fail | §5.2 | pre | C | `recvWire_rejects_nonminimal_16/64`(end-to-end, step 経由)— `decodeCanonical`→`parseFrame`→`recvWire` (Parse)。`f09_*_rejected_by_decode`/`f09_canonical_accepted` (Workflow/LengthCodec) が判定の根拠 |
| F-10 | 64bit 長の最上位ビットが1なら fail | §5.2 | pre | I | `recvWire_rejects_msb_64`(end-to-end, step 経由)(Parse)。`f10_msb_rejected_by_decode`/`decodeCanonical_rejects_msb_64` (Workflow/LengthCodec) が根拠 |
| F-11 | N メッセージ送信 → 順序通り N 受信 | §6 | temporal | I | `roundtrip_messages` (Trace) |

## C. 制御フレーム / タイミング (§5.4, §5.5)
| ID | 性質 | RFC | 型 | 優先 | 定理 (ファイル) |
|----|------|-----|----|----|----------------|
| C-01 | 制御フレーム割り込みが集約状態を壊さない | §5.4 | invariant | C | `control_preserves_acc` / `feed_filters_control` (Fragment) |
| C-02 | 制御フレームは payload ≤125 | §5.5 | pre | C | `control_oversize_rejected` (StateMachine) |
| C-03 | 制御フレームは fragment 不可(FIN=1 必須) | §5.5 | pre | C | `control_fragmented_rejected` (StateMachine) |
| C-04 | Ping 受信→Pong 応答義務 [^layer] | §5.5.2 | temporal | I | `ping_notified_with_payload` (Trace) |
| C-05 | Pong の payload = 元 Ping の payload | §5.5.2 | post | C | `pong_echoes_ping_payload` / `pong_payload_from_ping_event` (Workflow) |
| C-06 | unsolicited Pong を受理してよい | §5.5.3 | invariant | N | `unsolicited_pong_preserves_state` (Workflow) |
| C-07 | 受信粒度非依存: 任意チャンク分割で同一結果 | §6 | temporal | I | `chunking_invariant` / `split_join` (Trace) |

## D. マスク / エラー / close code (§5.1, §7.4, §8.1)
| ID | 性質 | RFC | 型 | 優先 | 定理 (ファイル) |
|----|------|-----|----|----|----------------|
| M-01 | server は masked クライアントフレームのみ受理 | §5.1 | invariant | C | `server_accepts_masked` (Workflow) |
| M-02 | unmasked クライアントフレーム→必ず fail | §5.1 | post | C | `server_rejects_unmasked` (StateMachine) |
| M-03 | server 送信フレームは必ず unmasked | §5.1 | invariant | C | `server_frames_unmasked` (Workflow) |
| M-04 | 全フレーム受信は accept か fail に決定的に分類 | §5/§7 | invariant | C | `error_implies_closed` (StateMachine) |
| M-05 | エラー検出→close→CLOSED の一方向 (Fail the Connection) | §7.1.7 | temporal | C | `error_implies_closed` (StateMachine) |
| M-06 | close code 1005/1006/1015 をフレームに出さない | §7.4.1 | invariant | I | `sanitized_close_code_not_reserved` (Workflow) |
| M-07 | 不正 close code 受信時は 1002 で fail | §7.4.1 | post | I | `step_close_validates` / `step_close_rejects_invalid` / `invalid_close_code_yields_1002` (StateMachine) |
| M-08 | 不正 UTF-8 の text/close reason は fail (1007) | §8.1 | post | C | `m08_utf8_decision_basis` — 既証 `validate_correct` 橋渡し (Workflow) |

[^layer]: C-04「層注」: pong の自動送信はアプリ責務。SDK(本モデル)は ping を payload 込みで
欠落・改変なく event 通知する所までを保証し、その通知の正確さを証明対象とする。

## C 実装との対応(test-first 橋渡し)

証明済みの step 関数/述語を `conn.c` の `consume_frame`/`header_ok`/`accept_data`/`accept_control`
の分岐に対応させ、`test_conn.c`/`test_frame.c` で固定する。テストが proof と実装の乖離を検出する。

### `step` に接続済み(モデルが C の分岐を直接表現)

`StateMachine.step`(`Spec/StateMachine.lean`)が判定する分岐は、対応する C コードの正準モデルになっている:
S-01/S-02(状態遷移・吸収)、F-07(RSV 拒否)、F-08(未知 opcode)、C-02/C-03(制御フレーム
fin/oversize)、M-02(unmasked 拒否)、S-04(CLOSING でデータ破棄)、M-07(受信 close code 検証:
`step` の `.close` 分岐が `recvCloseCode` で §7.4.1 域外を 1002 に写像。`step_close_validates`/
`step_close_rejects_invalid` が出力 code を固定し、`conn.c` の `close_code_from` と byte-for-byte 対応)。
これらは step を変えれば証明が落ちる。

### `step` に end-to-end 接続済み(ワイヤパーサ経由)

- **F-09/F-10**(長さデコード検証): ワイヤパーサ `Spec.Parse.parseFrame`(`frame.c` の
  `ws_frame_parse_header` 対応)が `LengthCodec.decodeCanonical` を呼び、非最小長(F-09)と
  64bit MSB 立ち(F-10)を拒否(`.error`)する。`recvWire = parseFrame ; step` の合成で、
  非最小/MSB 立ちのワイヤフレームが **`step` 経由で `(.closed, .error)` になる**ことを
  `recvWire_rejects_nonminimal_16/64`・`recvWire_rejects_msb_64`(end-to-end)で証明。
  これで decodeCanonical が状態機械に接続され、長さデコードのバグは Lean の証明を落とす。
  根拠の階層: `decodeCanonical_rejects_*`(拒否則)→ `parseFrame`(error 伝播)→ `recvWire`(step 合成)。
  `decodeCanonical_encode`(正準エンコードは必ず受理=健全性)が正常系の非空虚を保証する
  (正常フレームは `.ok` を返し step に到達)。
  スコープ: 証明が落とすのは**長さ値の拒否則**(非最小/MSB のワイヤ長は必ず `.error`→接続 fail)。
  `parseFrame` は `decodeCanonical (len7 :: rest)` と len7 を先頭バイトに再構成して呼ぶ(C `parse_len` は
  len7 を引数渡し・本体を `buf+2` から読む形だが、長さ判定結果は等価)。バイト不足を `.needMore` に倒す
  振り分け(C の NEED_MORE と ERROR の境界)は `parseFrame` では証明せず、`test_frame.c` の NEED_MORE/ERROR
  テストで担保する。payload のマスク解除と内容再現も別証明(`Masking.lean`)の領分として持ち込まない
  (payload はマスクされたままのバイト列)。
  なお `encode_injective`/`extendedLenOk` の純数論補題(`f09_minimal_length_basis`/`msb_set_len_rejected`)も併存。

### 乖離を炙り出して修正した例(test が先に落ち、実装修正で緑化)

- **S-04**: CLOSING でもデータを受理して MESSAGE を出していた → `state==OPEN` のみ `accept_data`。
- **M-07**: 受信 close code が無検証だった → §7.4.1 の許容域で検証、域外は 1002(`step` の `.close` 分岐へ接続済み)。
- **M-06**: `ws_send_close` が予約コードを載せ得た → 1000 へ丸める(送信経路は step 非経由だが M-06 定理と値が対応)。
- **F-09/F-10**: `frame.c` は既に非最小長・64bit MSB を拒否済み。`decodeCanonical` を正準モデルとして
  拒否則・健全性を証明し、さらにワイヤパーサ `parseFrame` 経由で `step` に end-to-end 接続
  (`recvWire_rejects_nonminimal_16/64`・`recvWire_rejects_msb_64`)。長さデコードのバグは Lean の証明を落とす。
