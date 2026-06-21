-- RFC6455 §5.4 フラグメント集約の正しさ。
-- データフレーム列の妥当性(WellFormedMsg)と、その集約(aggregate)の性質を証明する。
-- 制御フレーム割り込みが進行中メッセージのバイト列を変えないこと(C-01)も示す。
import WsProof.Spec.Core

namespace WsProof.Spec

/-- 集約後のメッセージ。op = 先頭フレームの opcode、payload = 連結バイト列。 -/
structure Message where
  op      : Opcode
  payload : List UInt8
  deriving Repr

/-- continuation の0個以上の中間フレーム列 + fin=true の末尾 continuation。
    `[末尾]` 単体(中間0個)から始まり、先頭に中間 continuation を積める。 -/
inductive WellFormedTail : List Frame → Prop where
  /-- 末尾フレーム: fin=true ∧ opcode=continuation。 -/
  | last (f : Frame)
      (hfin : f.fin = true)
      (hop : f.opcode = Opcode.continuation) :
      WellFormedTail [f]
  /-- 中間フレーム: fin=false ∧ opcode=continuation を前に積む。 -/
  | cont (f : Frame) (rest : List Frame)
      (hfin : f.fin = false)
      (hop : f.opcode = Opcode.continuation)
      (hrest : WellFormedTail rest) :
      WellFormedTail (f :: rest)

/-- フラグメント列の妥当性述語(RFC6455 §5.4)。 -/
inductive WellFormedMsg : List Frame → Prop where
  /-- [F-01] 単一フレーム: fin=true ∧ opcode∈{text,binary}。 -/
  | single (f : Frame)
      (hfin : f.fin = true)
      (hop : f.opcode = Opcode.text ∨ f.opcode = Opcode.binary) :
      WellFormedMsg [f]
  /-- [F-02] 複数フレーム: 先頭 fin=false ∧ opcode∈{text,binary}、残りは WellFormedTail。 -/
  | multi (f : Frame) (rest : List Frame)
      (hfin : f.fin = false)
      (hop : f.opcode = Opcode.text ∨ f.opcode = Opcode.binary)
      (hrest : WellFormedTail rest) :
      WellFormedMsg (f :: rest)

/-- フレーム列の payload を順序保存で連結する。 -/
def concatPayloads (fs : List Frame) : List UInt8 :=
  fs.flatMap (·.payload)

/-- 集約関数。WellFormedMsg を満たさない列(空など)は none。 -/
def aggregate : List Frame → Option Message
  | [] => none
  | f :: rest => some { op := f.opcode, payload := concatPayloads (f :: rest) }

/-- [F-06] WellFormedMsg なら aggregate は some を返し、op = 先頭フレームの opcode。
    先頭フレーム f0 とその後続 rest で fs = f0 :: rest と表せ、集約 op = f0.opcode。 -/
theorem aggregate_opcode {fs : List Frame} (h : WellFormedMsg fs) :
    ∃ f0 rest m, fs = f0 :: rest ∧ aggregate fs = some m ∧ m.op = f0.opcode := by
  cases h with
  | single f hfin hop =>
      exact ⟨f, [], _, rfl, rfl, rfl⟩
  | multi f rest hfin hop hrest =>
      exact ⟨f, rest, _, rfl, rfl, rfl⟩

/-- [F-05] 集約 payload = 各 payload の順序保存連結。 -/
theorem aggregate_payload_concat {fs : List Frame} (h : WellFormedMsg fs) :
    ∃ m, aggregate fs = some m ∧ m.payload = fs.flatMap (·.payload) := by
  cases h with
  | single f hfin hop =>
      exact ⟨_, rfl, rfl⟩
  | multi f rest hfin hop hrest =>
      exact ⟨_, rfl, rfl⟩

/-- 集約 payload の長さ = Σ 各 payload 長(連結の系)。 -/
theorem aggregate_length {fs : List Frame} (h : WellFormedMsg fs) :
    ∃ m, aggregate fs = some m ∧
      m.payload.length = (fs.map (·.payload.length)).sum := by
  obtain ⟨m, hagg, hpay⟩ := aggregate_payload_concat h
  refine ⟨m, hagg, ?_⟩
  rw [hpay]
  -- flatMap の長さ = 各長さの和
  induction fs with
  | nil => simp
  | cons g gs ih =>
      simp [List.flatMap_cons]

/-- [F-03] 文脈外 continuation: `[f]`(f.opcode=continuation)は WellFormedMsg でない。 -/
theorem continuation_needs_context (f : Frame) (hop : f.opcode = Opcode.continuation) :
    ¬ WellFormedMsg [f] := by
  intro h
  cases h with
  | single _ _ hop' =>
      rcases hop' with h1 | h1 <;> rw [hop] at h1 <;> exact absurd h1 (by decide)
  -- multi ケースは rest が空にならない([f] は要素1)ので到達不能だが、
  -- パターン上 cons f rest で rest=[] となり WellFormedTail [] は構成不能。
  | multi g rest hfin hop' hrest => cases hrest

/-- WellFormedTail の各フレームは opcode=continuation。 -/
theorem WellFormedTail.all_continuation {fs : List Frame} (h : WellFormedTail fs) :
    ∀ f ∈ fs, f.opcode = Opcode.continuation := by
  induction h with
  | last f hfin hop =>
      intro g hg
      simp only [List.mem_singleton] at hg
      subst hg; exact hop
  | cont f rest hfin hop hrest ih =>
      intro g hg
      simp only [List.mem_cons] at hg
      rcases hg with hg | hg
      · subst hg; exact hop
      · exact ih g hg

/-- [F-04 の言い換え] WellFormedMsg な列の中間(先頭以外)に
    text/binary(op≠continuation)が現れない。 -/
theorem no_interleave {fs : List Frame} (h : WellFormedMsg fs) :
    ∀ f ∈ fs.tail, f.opcode = Opcode.continuation := by
  cases h with
  | single f hfin hop =>
      intro g hg
      simp at hg
  | multi f rest hfin hop hrest =>
      intro g hg
      simp only [List.tail_cons] at hg
      exact hrest.all_continuation g hg

/-- データのみを acc に追加する(制御フレームも区別せず追加する素朴版)。 -/
def feedData (acc : List UInt8) (f : Frame) : List UInt8 :=
  acc ++ f.payload

/-- 集約状態への1フレーム投入。制御フレームは acc を変えない。 -/
def feed (acc : List UInt8) (f : Frame) : List UInt8 :=
  if f.opcode.isControl then acc else acc ++ f.payload

/-- [C-01] 制御フレームを注入しても acc は変わらない。 -/
theorem control_preserves_acc (acc : List UInt8) (f : Frame)
    (hctrl : f.opcode.isControl = true) :
    feed acc f = acc := by
  simp [feed, hctrl]

/-- フレーム列を feed で左畳み込みした結果 =
    制御フレームを除いたデータ列のみを ++ で連結した結果。
    制御の割り込みは集約に無影響。 -/
theorem feed_filters_control (acc : List UInt8) (fs : List Frame) :
    fs.foldl feed acc
      = acc ++ (fs.filter (fun f => ! f.opcode.isControl)).flatMap (·.payload) := by
  induction fs generalizing acc with
  | nil => simp
  | cons f rest ih =>
      simp only [List.foldl_cons, List.filter_cons]
      by_cases hc : f.opcode.isControl
      · -- 制御フレーム: feed は acc を変えず、filter からも除外。
        simp [feed, hc, ih]
      · -- データフレーム: feed が payload を追加、filter は残す。
        have hc' : f.opcode.isControl = false := by
          simpa using hc
        simp only [feed, hc', Bool.not_false, if_true]
        rw [ih]
        simp [List.flatMap_cons, List.append_assoc]
