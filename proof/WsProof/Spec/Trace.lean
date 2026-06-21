-- RFC6455 の時間的(trace)性質を List 上の述語・関数で形式化し証明する。
-- trace = イベント列 / フレーム列を List で表す。
-- 共通型は Core.lean を import して再利用する。
import WsProof.Spec.Core

namespace WsProof.Spec

/-! ## C-07 受信粒度非依存(最重要)

TCP はバイトストリームであり、1 フレームのバイト列がどのような境界で
チャンク分割されて到着しても、再構成後のバイト列は同一でなければならない。
モデル: チャンク列を `flatten`(= 旧 `List.join`)で連結したものが再構成結果。
-/

/-- チャンク列を連結して元のバイト列を再構成する。 -/
def reassemble (chunks : List (List UInt8)) : List UInt8 := chunks.flatten

/-- 同じ flatten を持つ 2 つの分割は、再構成結果が一致する。 -/
theorem chunking_invariant
    (c1 c2 : List (List UInt8)) (h : c1.flatten = c2.flatten) :
    reassemble c1 = reassemble c2 := by
  unfold reassemble
  exact h

/-- どんな分割でも、その flatten が元のバイト列に等しいなら、
    再構成すれば元のバイト列に戻る(受信粒度非依存の本質)。 -/
theorem split_join
    (bs : List UInt8) (chunks : List (List UInt8)) (h : chunks.flatten = bs) :
    reassemble chunks = bs := by
  unfold reassemble
  exact h

/-! ## F-11 メッセージ境界・順序保存

メッセージ列を送って受ける際、順序と個数が保存される。
最小モデル: 各メッセージを 1 フレーム(fin=true)へエンコードし、
列を連結して送り、受信側でデコードして元の列に戻す。
フラグメント化は本性質の本質ではないので落とす(下部の注記参照)。
-/

/-- アプリ層メッセージ。op と payload を持つ。 -/
structure Msg where
  op      : Opcode
  payload : List UInt8
  deriving Repr, DecidableEq

/-- メッセージを単一フレーム(fin=true, 非マスク)へエンコードする。 -/
def encode (m : Msg) : List Frame :=
  [{ fin := true, rsv1 := false, rsv2 := false, rsv3 := false,
     opcode := m.op, masked := false, payload := m.payload }]

/-- フレームを Msg へ戻す。 -/
def frameToMsg (f : Frame) : Msg := { op := f.opcode, payload := f.payload }

/-- フレーム列を Msg 列へデコードする(各フレーム = 1 メッセージ)。 -/
def decode (fs : List Frame) : List Msg := fs.map frameToMsg

/-- エンコード/デコードが往復し、順序と個数が保存される。 -/
theorem roundtrip_messages (msgs : List Msg) :
    decode (msgs.flatMap encode) = msgs := by
  unfold decode
  induction msgs with
  | nil => rfl
  | cons m ms ih =>
    simp [List.flatMap_cons, encode, frameToMsg, ih]

/-! ## S-06 close race 収束

両端が同時に Close を出しても、双方が CLOSED に収束する。
モデル: close 適用の状態遷移 open→closing→closed、closed は吸収状態。
-/

/-- close 適用による状態遷移。 -/
def applyClose : State → State
  | .open    => .closing
  | .closing => .closed
  | .closed  => .closed

/-- closed は close に対して吸収的(これ以上変化しない)。 -/
theorem closed_absorbing_close : applyClose .closed = .closed := rfl

/-- どの状態からでも close を 2 回適用すれば必ず closed に到達する。
    close race(両端 close)でも収束することを保証する。 -/
theorem close_race_converges (s : State) :
    applyClose (applyClose s) = .closed := by
  cases s <;> rfl

/-! ## C-04 ping→pong 応答義務(層注つき)

層注: pong の自動送信はアプリ層の責務である。
SDK(本モデル)が保証するのは「ping を payload 込みで欠落・改変なく
event 通知する」ところまで。アプリはこの event を受けて pong を送る。
ここではその正確な通知のみを形式化・証明する。
-/

/-- フレームを外部通知イベントへ変換する(ping/pong は payload 込み)。 -/
def notify (f : Frame) : Event :=
  match f.opcode with
  | .ping => .ping f.payload
  | .pong => .pong f.payload
  | .close => .close 0
  | op     => .message op f.payload

/-- ping フレームは、payload を欠落・改変なく `.ping` event として通知される。 -/
theorem ping_notified_with_payload (f : Frame) (h : f.opcode = .ping) :
    notify f = .ping f.payload := by
  unfold notify
  rw [h]

end WsProof.Spec
