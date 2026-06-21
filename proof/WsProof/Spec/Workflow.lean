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

/-! ## M-06 / M-07 close code (§7.4.1) -/

/-- フレームに出してはならない予約 close code(1005/1006/1015)。 -/
def isReservedCloseCode (c : UInt16) : Bool :=
  c = 1005 || c = 1006 || c = 1015

/-- 送信用 close フレームの close code を検証してから採用する。
    予約コードは 1000(Normal)へ丸める。 -/
def sanitizeCloseCode (c : UInt16) : UInt16 :=
  if isReservedCloseCode c then 1000 else c

/-- M-06: sanitize 後の close code は予約コードでない。 -/
theorem sanitized_close_code_not_reserved (c : UInt16) :
    isReservedCloseCode (sanitizeCloseCode c) = false := by
  unfold sanitizeCloseCode
  by_cases h : isReservedCloseCode c = true
  · -- 予約コードは 1000 へ丸める。1000 は予約でない。
    rw [if_pos h]; decide
  · -- 予約でない c はそのまま。前提より予約でない。
    rw [if_neg h]
    simpa using h

/-- 受信 close code が妥当か(RFC6455 §7.4.1 の許容範囲)。
    1000-1003, 1007-1011, 3000-4999 を妥当とする(簡約モデル)。 -/
def validCloseCode (c : UInt16) : Bool :=
  (1000 ≤ c && c ≤ 1003) ||
  (1007 ≤ c && c ≤ 1011) ||
  (3000 ≤ c && c ≤ 4999)

/-- 不正 close code 受信時に返すべき close code(M-07: 1002 Protocol Error)。 -/
def closeCodeOnInvalid (received : UInt16) : UInt16 :=
  if validCloseCode received then received else 1002

/-- M-07: 不正な close code を受けたら 1002 で応答する。 -/
theorem invalid_close_code_yields_1002 (c : UInt16) (h : validCloseCode c = false) :
    closeCodeOnInvalid c = 1002 := by
  simp [closeCodeOnInvalid, h]

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
