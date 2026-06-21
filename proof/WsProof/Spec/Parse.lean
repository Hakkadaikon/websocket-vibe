/-
ワイヤバイト列 → Frame のパーサ `parseFrame` と、`step` と合成した `recvWire`。

これまで F-09(非最小長を fail)/ F-10(64bit 長 MSB 立ちを fail)は
`LengthCodec.decodeCanonical` で「どのバイト列を拒否するか」だけ証明済みで、
`step` / `Frame` は長さをパース済み payload として持ち decodeCanonical を呼ばなかった。
よって「非最小長のワイヤフレームが step 経由で接続 fail する」という
end-to-end の保証が無かった。

このファイルは `parseFrame : List UInt8 → ParseResult` を定義し、長さデコードに
`decodeCanonical` を使う。decodeCanonical が none(非最小 / MSB 立ち)を返したら
parseFrame は `.error` を返す。`recvWire` はそれを `(.closed, .error)` に写す。
これで decodeCanonical の拒否則が step 経由で end-to-end に効く。

C 実装 `src/core/frame.c` の `ws_frame_parse_header` / `parse_len` / `parse_meta`
の長さデコード・拒否経路に一致させる:
  * len < 2                 → needMore
  * len7 == 126, v < 126    → error (parse_len16)
  * len7 == 127, MSB 立ち or v ≤ 0xFFFF → error (check_len64)
  * ヘッダ全体(マスクキー含む)+ payload が揃わなければ needMore

スコープ(YAGNI): マスク解除(unmask)・payload 内容の完全再現はここでは行わない
(別証明 Masking.lean の領分)。payload にはマスクされたままのバイトが入る。
主眼は「長さデコードが error を出す経路」を C と正確に一致させること。

`sorry` / `axiom` 無し。主要定理は `#print axioms` で sorryAx 非依存を確認できる。
-/
import WsProof.Spec.Core
import WsProof.Spec.StateMachine
import WsProof.LengthCodec

namespace WsProof.Spec

open WsProof.LengthCodec

/-- パース結果。C の `ws_parse_status`(NEED_MORE / ERROR / OK)に対応。 -/
inductive ParseResult where
  | needMore
  | error
  | ok (f : Frame) (rest : List UInt8)
  deriving Repr

/-- opcode の下位 4bit 値 → Core.Opcode。frame.c の opcode 数値対応に一致。 -/
def toOpcode (n : Nat) : Opcode :=
  match n with
  | 0x0 => .continuation
  | 0x1 => .text
  | 0x2 => .binary
  | 0x8 => .close
  | 0x9 => .ping
  | 0xA => .pong
  | v   => .reserved v

/-- リストの先頭 `n` 要素と残りを返す。長さ不足なら none(needMore 判定に使う)。 -/
def takeExact (n : Nat) (bs : List UInt8) : Option (List UInt8 × List UInt8) :=
  if bs.length < n then none else some (bs.take n, bs.drop n)

/-- ワイヤバイト列をパースする。

第1バイト b0 から FIN/RSV/opcode、第2バイト b1 から mask フラグと len7 を取り出す。
長さは `decodeCanonical (b1 :: rest)` で正準デコードする(b1 が len7 マーカー or 直接長)。
decodeCanonical が none(非最小 / MSB 立ち / 長さ部バイト不足)を返したら error。
(C `parse_len` は len7 を引数・本体を buf+2 から読むが、長さ判定の結果は本再構成と等価。
バイト不足を needMore に倒す境界は本モデルでは証明せず test_frame.c で担保する。)

長さデコード後、マスクキー(masked なら 4 バイト)+ payload(plen バイト)を
切り出す。バイト不足なら needMore。

payload はマスクされたままのバイト列(unmask はスコープ外)。 -/
def parseFrame (bs : List UInt8) : ParseResult :=
  match bs with
  | b0 :: b1 :: rest =>
    let fin    := (b0.toNat &&& 0x80) != 0
    let rsv1   := (b0.toNat &&& 0x40) != 0
    let rsv2   := (b0.toNat &&& 0x20) != 0
    let rsv3   := (b0.toNat &&& 0x10) != 0
    let opc    := toOpcode (b0.toNat &&& 0x0F)
    let masked := (b1.toNat &&& 0x80) != 0
    let len7   := b1.toNat &&& 0x7F
    -- decodeCanonical は先頭バイト(len7 マーカー含む)から取る。
    -- b1 の下位 7bit を表すバイトを先頭に置いて呼ぶ。
    match decodeCanonical (UInt8.ofNat len7 :: rest) with
    | none => .error
    | some (plen, afterLen) =>
      -- マスクキー(masked なら 4 バイト)。
      match takeExact (if masked then 4 else 0) afterLen with
      | none => .needMore
      | some (_maskKey, afterMask) =>
        -- payload(plen バイト)。
        match takeExact plen afterMask with
        | none => .needMore
        | some (payload, tail) =>
          .ok { fin := fin, rsv1 := rsv1, rsv2 := rsv2, rsv3 := rsv3,
                opcode := opc, masked := masked, payload := payload } tail
  | _ => .needMore

/-- ワイヤバイト列を受けて状態機械を 1 ステップ進める。
parseFrame と step の合成。error は接続 fail、needMore は無動作。 -/
def recvWire (role : Role) (s : State) (inMsg : Bool) (bs : List UInt8) : State × Event :=
  match parseFrame bs with
  | .ok f _   => step role s inMsg f
  | .error    => (.closed, .error)
  | .needMore => (s, .none)

/-! ## F-09 / F-10 end-to-end 定理

parseFrame が decodeCanonical の none を `.error` に伝播し、recvWire がそれを
`(.closed, .error)` に写すことを使う。decodeCanonical の既証拒否則を再利用する。 -/

/-- `decodeCanonical` の拒否則は先頭バイトに `126`/`127` リテラルを期待する。
parseFrame 内では `UInt8.ofNat len7` の形で現れるので、リテラルへ正規化する補助。 -/
private theorem ofNat_126_eq : (UInt8.ofNat 126) = 126 := by decide
private theorem ofNat_127_eq : (UInt8.ofNat 127) = 127 := by decide

/-- parseFrame が error を返すなら recvWire は必ず `(.closed, .error)`。 -/
theorem recvWire_error_of_parse_error (role : Role) (s : State) (inMsg : Bool)
    (bs : List UInt8) (h : parseFrame bs = .error) :
    recvWire role s inMsg bs = (.closed, .error) := by
  unfold recvWire
  rw [h]

/-- **F-09 end-to-end(16bit 非最小)**: 126 形式で 2 バイト長 `< 126` のワイヤフレームは
`recvWire` で必ず `(.closed, .error)`。`b0` は任意、長さ部 `[hi, lo]` の BE 値が `< 126`。 -/
theorem recvWire_rejects_nonminimal_16 (role : Role) (s : State) (inMsg : Bool)
    (b0 hi lo : UInt8) (rest : List UInt8)
    (hlen : fromBE [hi, lo] < 126) :
    recvWire role s inMsg (b0 :: 126 :: hi :: lo :: rest) = (.closed, .error) := by
  apply recvWire_error_of_parse_error
  unfold parseFrame
  -- b1 = 126: len7 = 126 &&& 0x7F = 126
  have hlen7 : (126 : UInt8).toNat &&& 0x7F = 126 := by decide
  simp only [hlen7, ofNat_126_eq]
  rw [decodeCanonical_rejects_nonminimal_16 hi lo rest hlen]

/-- **F-10 end-to-end(MSB 立ち)**: 127 形式で 8 バイト長の MSB が立つワイヤフレームは
`recvWire` で必ず `(.closed, .error)`。 -/
theorem recvWire_rejects_msb_64 (role : Role) (s : State) (inMsg : Bool)
    (b0 c7 c6 c5 c4 c3 c2 c1 c0 : UInt8) (rest : List UInt8)
    (hmsb : fromBE [c7, c6, c5, c4, c3, c2, c1, c0] ≥ msbThreshold64) :
    recvWire role s inMsg
        (b0 :: 127 :: c7 :: c6 :: c5 :: c4 :: c3 :: c2 :: c1 :: c0 :: rest)
      = (.closed, .error) := by
  apply recvWire_error_of_parse_error
  unfold parseFrame
  have hlen7 : (127 : UInt8).toNat &&& 0x7F = 127 := by decide
  simp only [hlen7, ofNat_127_eq]
  rw [decodeCanonical_rejects_msb_64 c7 c6 c5 c4 c3 c2 c1 c0 rest hmsb]

/-- **F-09 end-to-end(64bit 非最小)**: 127 形式で MSB が立たず 8 バイト長 `≤ 65535` の
ワイヤフレームは `recvWire` で必ず `(.closed, .error)`(2 バイト形式を使うべき非最小)。 -/
theorem recvWire_rejects_nonminimal_64 (role : Role) (s : State) (inMsg : Bool)
    (b0 c7 c6 c5 c4 c3 c2 c1 c0 : UInt8) (rest : List UInt8)
    (hmsb : fromBE [c7, c6, c5, c4, c3, c2, c1, c0] < msbThreshold64)
    (hmin : fromBE [c7, c6, c5, c4, c3, c2, c1, c0] ≤ 65535) :
    recvWire role s inMsg
        (b0 :: 127 :: c7 :: c6 :: c5 :: c4 :: c3 :: c2 :: c1 :: c0 :: rest)
      = (.closed, .error) := by
  apply recvWire_error_of_parse_error
  unfold parseFrame
  have hlen7 : (127 : UInt8).toNat &&& 0x7F = 127 := by decide
  simp only [hlen7, ofNat_127_eq]
  rw [decodeCanonical_rejects_nonminimal_64 c7 c6 c5 c4 c3 c2 c1 c0 rest hmsb hmin]

/-- **F-10 concrete**: 最小の MSB 立ち 8 バイト長 `0x8000…0000` の具体ワイヤフレームは
`recvWire` で `(.closed, .error)`。直接評価で確定(native_decide 不使用)。 -/
theorem recvWire_rejects_msb_concrete (role : Role) (s : State) (inMsg : Bool) :
    recvWire role s inMsg [0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 0] = (.closed, .error) := by
  apply recvWire_error_of_parse_error
  rfl

end WsProof.Spec
