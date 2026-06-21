-- RFC6455 状態機械の安全性。conn.c の header_ok/consume_frame/accept_control に対応。
-- 決定的な単一ステップ関数 step を定義し、その安全性を証明する。
-- Mathlib 無し。decide / 場合分け / omega を活用。
import WsProof.Spec.Core

namespace WsProof.Spec

/-- close フレームの payload からクローズコードを取り出す。
    先頭 2 byte を big-endian u16 として解釈。長さ < 2 なら 1005(No Status Rcvd)。 -/
def closeCode (payload : List UInt8) : UInt16 :=
  match payload with
  | hi :: lo :: _ => (hi.toUInt16 <<< 8) ||| lo.toUInt16
  | _ => 1005

/-- 制御フレームが妥当か(fin=true かつ payload 長 ≤ 125)。 -/
def controlFrameOk (f : Frame) : Bool :=
  f.fin && (f.payload.length ≤ 125)

/-- 決定的な単一ステップ。conn.c の 1 フレーム処理に対応。
    inMsg はメッセージ集約中(継続フレーム待ち)かどうか。F-03/F-04 の表現に使う。 -/
def step (role : Role) (s : State) (inMsg : Bool) (f : Frame) : State × Event :=
  -- closed は吸収状態。何が来ても (closed, none)。
  match s with
  | .closed => (.closed, .none)
  | _ =>
    -- M-02: server は unmasked フレームを拒否。
    if role = .server ∧ f.masked = false then
      (.closed, .error)
    -- F-07: RSV ビットが立っていたら拒否。
    else if f.rsv1 ∨ f.rsv2 ∨ f.rsv3 then
      (.closed, .error)
    else
      match f.opcode with
      -- F-08: 未知/予約 opcode は拒否。
      | .reserved _ => (.closed, .error)
      -- 制御フレーム。
      | .close =>
        -- C-02/C-03: 制御フレームの fin=false / oversize は拒否。
        if controlFrameOk f = false then (.closed, .error)
        else
          match s with
          | .open => (.closing, .close (closeCode f.payload))
          | _     => (.closed, .close (closeCode f.payload))
      | .ping =>
        if controlFrameOk f = false then (.closed, .error)
        else (s, .ping f.payload)
      | .pong =>
        if controlFrameOk f = false then (.closed, .error)
        else (s, .pong f.payload)
      -- データフレーム。F-03/F-04: 継続文脈の整合。
      | .continuation =>
        if inMsg = true then (s, .none) else (.closed, .error)
      | .text =>
        if inMsg = false then (s, .none) else (.closed, .error)
      | .binary =>
        if inMsg = false then (s, .none) else (.closed, .error)

/-- 状態の順序付け。open < closing < closed。逆行しないことの証明に使う。 -/
def State.rank : State → Nat
  | .open    => 0
  | .closing => 1
  | .closed  => 2

/-! ## 安全性定理 -/

/-- S-02: closed は吸収状態。closed からは必ず closed に留まる。 -/
theorem step_closed_absorbing (role : Role) (inMsg : Bool) (f : Frame) :
    (step role .closed inMsg f).1 = .closed := by
  rfl

/-- S-01: 状態は逆行しない(rank 単調)。 -/
theorem step_monotone (role : Role) (s : State) (inMsg : Bool) (f : Frame) :
    s.rank ≤ (step role s inMsg f).1.rank := by
  cases s with
  | closed => simp [step, State.rank]
  | «open» =>
    simp only [State.rank]; exact Nat.zero_le _
  | closing =>
    have : (step role .closing inMsg f).1 = .closing ∨ (step role .closing inMsg f).1 = .closed := by
      unfold step
      repeat' split
      all_goals simp_all
    rcases this with h | h <;> rw [h] <;> simp [State.rank]

/-- M-02: server は unmasked フレームを(closed 以外の状態で)拒否し closed/error にする。 -/
theorem server_rejects_unmasked (s : State) (inMsg : Bool) (f : Frame)
    (hmask : f.masked = false) (hns : s ≠ .closed) :
    step .server s inMsg f = (.closed, .error) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» => simp [step, hmask]
  | closing => simp [step, hmask]

/-- M-04/M-05: error を返すなら新状態は必ず closed。
    「accept なら状態維持/遷移、reject なら必ず閉じる」の核。 -/
theorem error_implies_closed (role : Role) (s : State) (inMsg : Bool) (f : Frame)
    (herr : (step role s inMsg f).2 = .error) :
    (step role s inMsg f).1 = .closed := by
  unfold step at herr ⊢
  cases s with
  | closed => simp at herr
  | «open» =>
    revert herr
    repeat' split
    all_goals (intro h; first | rfl | simp_all)
  | closing =>
    revert herr
    repeat' split
    all_goals (intro h; first | rfl | simp_all)

/-- C-02: 制御フレームで payload 長 > 125 なら拒否。 -/
theorem control_oversize_rejected (role : Role) (s : State) (inMsg : Bool) (f : Frame)
    (hns : s ≠ .closed)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hctrl : f.opcode.isControl = true)
    (hsize : f.payload.length > 125) :
    step role s inMsg f = (.closed, .error) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» =>
    simp only [step]
    rw [if_neg hmask, if_neg hrsv]
    cases hop : f.opcode <;> simp_all [Opcode.isControl, controlFrameOk] <;> omega
  | closing =>
    simp only [step]
    rw [if_neg hmask, if_neg hrsv]
    cases hop : f.opcode <;> simp_all [Opcode.isControl, controlFrameOk] <;> omega

/-- C-03: 制御フレームで fin=false(fragmented)なら拒否。 -/
theorem control_fragmented_rejected (role : Role) (s : State) (inMsg : Bool) (f : Frame)
    (hns : s ≠ .closed)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hctrl : f.opcode.isControl = true)
    (hfin : f.fin = false) :
    step role s inMsg f = (.closed, .error) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» =>
    simp only [step]
    rw [if_neg hmask, if_neg hrsv]
    cases hop : f.opcode <;> simp_all [Opcode.isControl, controlFrameOk]
  | closing =>
    simp only [step]
    rw [if_neg hmask, if_neg hrsv]
    cases hop : f.opcode <;> simp_all [Opcode.isControl, controlFrameOk]

/-- F-07: RSV ビットが立っていれば(closed 以外で)拒否。 -/
theorem rsv_nonzero_rejected (role : Role) (s : State) (inMsg : Bool) (f : Frame)
    (hns : s ≠ .closed)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : f.rsv1 ∨ f.rsv2 ∨ f.rsv3) :
    step role s inMsg f = (.closed, .error) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» => simp only [step]; rw [if_neg hmask, if_pos hrsv]
  | closing => simp only [step]; rw [if_neg hmask, if_pos hrsv]

/-- F-08: 未知/予約 opcode は(closed 以外で)拒否。 -/
theorem unknown_opcode_rejected (role : Role) (s : State) (inMsg : Bool) (f : Frame) (v : Nat)
    (hns : s ≠ .closed)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hop : f.opcode = .reserved v) :
    step role s inMsg f = (.closed, .error) := by
  cases s with
  | closed => exact absurd rfl hns
  | «open» => simp only [step]; rw [if_neg hmask, if_neg hrsv, hop]
  | closing => simp only [step]; rw [if_neg hmask, if_neg hrsv, hop]

/-- S-05 への布石: open + 妥当な close フレーム → closing へ遷移。 -/
theorem close_from_open (role : Role) (inMsg : Bool) (f : Frame)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hop : f.opcode = .close)
    (hok : controlFrameOk f = true) :
    step role .open inMsg f = (.closing, .close (closeCode f.payload)) := by
  simp only [step]
  rw [if_neg hmask, if_neg hrsv, hop]
  simp [hok]

/-- closing + 妥当な close フレーム → closed へ遷移。 -/
theorem close_from_closing (role : Role) (inMsg : Bool) (f : Frame)
    (hmask : ¬(role = .server ∧ f.masked = false))
    (hrsv : ¬(f.rsv1 ∨ f.rsv2 ∨ f.rsv3))
    (hop : f.opcode = .close)
    (hok : controlFrameOk f = true) :
    step role .closing inMsg f = (.closed, .close (closeCode f.payload)) := by
  simp only [step]
  rw [if_neg hmask, if_neg hrsv, hop]
  simp [hok]

end WsProof.Spec
