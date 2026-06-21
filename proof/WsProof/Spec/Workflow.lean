-- RFC6455 のワークフロー/タイミング/マスク・close code 系の残り性質。
-- StateMachine.step / Trace.notify を再利用し、28 要素カタログの未証明分を埋める。
-- 既証(Masking/LengthCodec/Utf8)は「橋渡し」定理として明示参照する。
import WsProof.Spec.StateMachine
import WsProof.Spec.Trace
import WsProof.LengthCodec
import WsProof.Utf8

namespace WsProof.Spec

/-! ## S-03 Close 送信後はデータフレームを送らない (§5.5.1)

送信側モデル: 状態に応じて「データ送信が許されるか」を判定する述語。
RFC: Close を送る = open→closing。以降データ送信は禁止。 -/

/-- その状態でデータフレーム送信が許されるか。open のみ許可。 -/
def maySendData : State → Bool
  | .open => true
  | _     => false

/-- S-03: closing/closed(= Close 送信後)ではデータ送信は許されない。 -/
theorem no_data_after_close_sent (s : State) (h : s ≠ .open) :
    maySendData s = false := by
  cases s with
  | «open» => exact absurd rfl h
  | closing => rfl
  | closed => rfl

/-! ## S-04 Close 受信後は以降のデータを破棄する (§5.5.1)

step が closing(= Close 受信済み)でデータフレームを受けても、
message event を生成せず state を変えない = 破棄。 -/

/-- S-04: closing 状態で(集約文脈外の)データフレームは破棄される
    (state 維持・message を出さない)。 -/
theorem data_discarded_after_close_recv (role : Role) (f : Frame)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hdata : f.opcode = .text ∨ f.opcode = .binary) :
    step role .closing false f = (.closing, .none) := by
  simp only [step]
  rw [if_neg hmask, if_neg hrsv]
  rcases hdata with h | h <;> rw [h] <;> simp

/-! ## C-05 Pong の payload = 元 Ping の payload (§5.5.2)

層注: pong 自動送信はアプリ責務。ここでは「ping event の payload を
そのまま pong フレームに載せれば payload が一致する」ことを示す。 -/

/-- ping フレームを受けて返すべき pong フレームを構築する。 -/
def pongFor (ping : Frame) : Frame :=
  { fin := true, rsv1 := false, rsv2 := false, rsv3 := false,
    opcode := .pong, masked := false, payload := ping.payload }

/-- C-05: 構築した pong の payload は元の ping の payload に等しい。 -/
theorem pong_echoes_ping_payload (ping : Frame) :
    (pongFor ping).payload = ping.payload := rfl

/-- C-05(event 経由): ping event の payload を pong に載せれば一致。 -/
theorem pong_payload_from_ping_event (f : Frame) (h : f.opcode = .ping) :
    notify f = .ping (pongFor f).payload := by
  rw [ping_notified_with_payload f h, pong_echoes_ping_payload]

/-! ## C-06 unsolicited Pong を受理してよい(状態を壊さない)(§5.5.3 ) -/

/-- C-06: 妥当な pong フレームは state を変えず、pong event として通知される。 -/
theorem unsolicited_pong_preserves_state (role : Role) (s : State) (inMsg : Bool) (f : Frame)
    (hns : s ≠ .closed)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hop : f.opcode = .pong)
    (hok : controlFrameOk f = true) :
    step role s inMsg f = (s, .pong f.payload) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» => simp only [step]; rw [if_neg hmask, if_neg hrsv, hop]; simp [hok]
  | closing => simp only [step]; rw [if_neg hmask, if_neg hrsv, hop]; simp [hok]

/-! ## M-01 / M-03 マスク方向 (§5.1) -/

/-- M-01: server は masked フレームのみ受理(unmasked は別定理 server_rejects_unmasked で拒否)。
    masked フレームは「マスク理由では」拒否されない、を masked=true 前提で示す。 -/
theorem server_accepts_masked (s : State) (inMsg : Bool) (f : Frame)
    (hns : s ≠ .closed)
    (hmask : f.masked = true)
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hop : f.opcode = .ping)
    (hok : controlFrameOk f = true) :
    step .server s inMsg f = (s, .ping f.payload) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» =>
    simp only [step]
    rw [if_neg (by simp [hmask]), if_neg hrsv, hop]; simp [hok]
  | closing =>
    simp only [step]
    rw [if_neg (by simp [hmask]), if_neg hrsv, hop]; simp [hok]

/-- server が送信するフレームを構築する(M-03: 必ず unmasked)。 -/
def serverFrame (op : Opcode) (payload : List UInt8) (fin : Bool) : Frame :=
  { fin := fin, rsv1 := false, rsv2 := false, rsv3 := false,
    opcode := op, masked := false, payload := payload }

/-- M-03: server が構築する全フレームは unmasked。 -/
theorem server_frames_unmasked (op : Opcode) (payload : List UInt8) (fin : Bool) :
    (serverFrame op payload fin).masked = false := rfl

/-! ## M-06 / M-07 close code (§7.4.1)

定義と定理は StateMachine.lean へ移動済み(step の close 分岐検証で再利用するため)。
`validCloseCode`/`closeCodeOnInvalid`/`isReservedCloseCode`/`sanitizeCloseCode` と
M-06(`sanitized_close_code_not_reserved`)/M-07(`invalid_close_code_yields_1002`)、
step 接続定理(`step_close_validates`/`step_close_rejects_invalid`)を参照すること。 -/

/-! ## F-10 64bit 拡張長の最上位ビット (§5.2) -/

/-- 64bit 拡張ペイロード長の最上位ビット(bit63)の閾値。 -/
def msbThreshold : Nat := 1 <<< 63

/-- 64bit 拡張ペイロード長の最上位ビットが立っていれば fail。
    判定は toNat 上の大小比較で行う。 -/
def extendedLenOk (len : UInt64) : Bool :=
  len.toNat < msbThreshold

/-- F-10: MSB(bit63)が立った 64bit 長は拒否される。 -/
theorem msb_set_len_rejected (len : UInt64) (h : len.toNat ≥ msbThreshold) :
    extendedLenOk len = false := by
  simp only [extendedLenOk, decide_eq_false_iff_not, Nat.not_lt]
  exact h

/-! ## F-09/F-10 の主たる根拠: 正準デコード判定 `decodeCanonical`

`LengthCodec.decodeCanonical` が C 実装 `src/core/frame.c` の `parse_len`
(`parse_len16`/`parse_len64`/`check_len64`)の正準モデルである。
byte-for-byte に対応し、非最小エンコード(F-09)と bit63 立ち(F-10)を `none` で拒否する。
上の `extendedLenOk`/`msbThreshold` は MSB 判定の意味的核として併存させるが、
実際の長さデコード判定としての F-09/F-10 の根拠はここの定理に置く。 -/

/-- F-10(主): 127 形式の 8byte 長が bit63 を立てていれば(`≥ 2^63`)正準デコードは拒否する。
    C `check_len64` の `v & 0x8000…` ガードに対応。 -/
theorem f10_msb_rejected_by_decode
    (b7 b6 b5 b4 b3 b2 b1 b0 : UInt8) (rest : List UInt8)
    (hmsb : LengthCodec.fromBE [b7, b6, b5, b4, b3, b2, b1, b0]
              ≥ LengthCodec.msbThreshold64) :
    LengthCodec.decodeCanonical
        (127 :: b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest) = none :=
  LengthCodec.decodeCanonical_rejects_msb_64 b7 b6 b5 b4 b3 b2 b1 b0 rest hmsb

/-- F-10(具体): 最小の bit63 立ち長 `0x8000…0000` は拒否される。 -/
theorem f10_msb_rejected_concrete :
    LengthCodec.decodeCanonical [127, 0x80, 0, 0, 0, 0, 0, 0, 0] = none :=
  LengthCodec.decodeCanonical_rejects_msb_concrete

/-- F-09(主, 16bit 形式): 126 形式に 126 未満の値を入れた非最小エンコードは拒否される。
    C `parse_len16` の `v < WS_LEN7_16BIT` ガードに対応。 -/
theorem f09_nonminimal16_rejected_by_decode
    (b1 b0 : UInt8) (rest : List UInt8)
    (h : LengthCodec.fromBE [b1, b0] < 126) :
    LengthCodec.decodeCanonical (126 :: b1 :: b0 :: rest) = none :=
  LengthCodec.decodeCanonical_rejects_nonminimal_16 b1 b0 rest h

/-- F-09(主, 64bit 形式): 127 形式に 65535 以下の値を入れた非最小エンコードは拒否される。
    C `check_len64` の `v <= WS_LEN16_MAX` ガードに対応。 -/
theorem f09_nonminimal64_rejected_by_decode
    (b7 b6 b5 b4 b3 b2 b1 b0 : UInt8) (rest : List UInt8)
    (hmsb : LengthCodec.fromBE [b7, b6, b5, b4, b3, b2, b1, b0]
              < LengthCodec.msbThreshold64)
    (hmin : LengthCodec.fromBE [b7, b6, b5, b4, b3, b2, b1, b0] ≤ 65535) :
    LengthCodec.decodeCanonical
        (127 :: b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest) = none :=
  LengthCodec.decodeCanonical_rejects_nonminimal_64
    b7 b6 b5 b4 b3 b2 b1 b0 rest hmsb hmin

/-- F-09/F-10(健全性): 正準エンコード(`encode`、常に最小形式)は必ず正準デコードで
    受理される(`n < 2^63`)。拒否則が正当な値を巻き込まないことの保証。 -/
theorem f09_canonical_accepted (n : Nat) (rest : List UInt8) (hn : n < 2 ^ 63) :
    LengthCodec.decodeCanonical (LengthCodec.encode n ++ rest) = some (n, rest) :=
  LengthCodec.decodeCanonical_encode n rest hn

/-! ## 橋渡し定理(既証の数学性質を仕様要素へ接続)

F-09(非最小長 fail)と M-08(不正 UTF-8 fail)は、それぞれ
LengthCodec の単射性 / Utf8 の健全性を「判定の核」として使う。
ここで既証定理を再エクスポートし、仕様要素 ID と対応づける。 -/

/-- F-09 橋渡し: payload 長エンコードは単射。同じ長さに 2 通りの符号は無く、
    非最小長を別表現として受理しないことの数学的根拠。
    既証 `LengthCodec.encode_injective` を仕様 ID へ接続する。 -/
theorem f09_minimal_length_basis (n m : Nat) (hn : n < 2 ^ 64) (hm : m < 2 ^ 64)
    (h : LengthCodec.encode n = LengthCodec.encode m) : n = m :=
  LengthCodec.encode_injective n m hn hm h

/-- M-08 橋渡し: text/close reason の UTF-8 判定は健全かつ完全。
    `validate bs = true ↔ WellFormed bs`。不正(¬WellFormed)なら validate=false で
    fail(1007)。既証 `Utf8.validate_correct` を仕様 ID へ接続する。 -/
theorem m08_utf8_decision_basis (bs : List UInt8) :
    Utf8.validate bs = true ↔ Utf8.WellFormed bs :=
  Utf8.validate_correct bs

end WsProof.Spec
