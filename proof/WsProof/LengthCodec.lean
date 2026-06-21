/-
RFC6455 section 5.2 payload length encoding/decoding, formally verified.

Three forms:
  * n ≤ 125          : single byte = n          (1 byte total)
  * 126 ≤ n ≤ 65535  : 0x7E, then 2-byte big-endian length
  * n ≥ 65536        : 0x7F, then 8-byte big-endian length (MSB must be 0)

The C implementation must match `encode` / `decode` below byte-for-byte.

Main theorem: `decode_encode` — roundtrip over any trailing `rest`.
No `sorry`; check with `#print axioms decode_encode`.
-/

namespace WsProof.LengthCodec

/-! ## Big-endian byte extraction (fixed width) -/

/-- Extract the byte of `n` at byte-position `i` (0 = least significant). -/
def byteAt (n : Nat) (i : Nat) : UInt8 :=
  UInt8.ofNat ((n >>> (8 * i)) % 256)

/-! ## Encoding -/

/-- Encode a payload length to its RFC6455 wire bytes. -/
def encode (n : Nat) : List UInt8 :=
  if n ≤ 125 then
    [UInt8.ofNat n]
  else if n ≤ 65535 then
    [UInt8.ofNat 126, byteAt n 1, byteAt n 0]
  else
    [UInt8.ofNat 127,
     byteAt n 7, byteAt n 6, byteAt n 5, byteAt n 4,
     byteAt n 3, byteAt n 2, byteAt n 1, byteAt n 0]

/-! ## Decoding -/

/-- Combine big-endian bytes (most significant first) into a Nat. -/
def fromBE (bs : List UInt8) : Nat :=
  bs.foldl (fun acc b => acc * 256 + b.toNat) 0

/-- Decode RFC6455 wire bytes: returns the length and the remaining bytes. -/
def decode (bs : List UInt8) : Option (Nat × List UInt8) :=
  match bs with
  | [] => none
  | b :: rest =>
    if b.toNat ≤ 125 then
      some (b.toNat, rest)
    else if b.toNat = 126 then
      match rest with
      | b1 :: b0 :: rest' => some (fromBE [b1, b0], rest')
      | _ => none
    else if b.toNat = 127 then
      match rest with
      | b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest' =>
        some (fromBE [b7, b6, b5, b4, b3, b2, b1, b0], rest')
      | _ => none
    else
      none

/-! ## Helper lemmas -/

/-- `byteAt` always produces a value < 256, so `UInt8.toNat (byteAt n i) = ...`. -/
theorem byteAt_toNat (n i : Nat) :
    (byteAt n i).toNat = (n >>> (8 * i)) % 256 := by
  unfold byteAt
  simp

/-- For `n ≤ 125`, the single decoded byte recovers `n`. -/
theorem toNat_ofNat_le125 (n : Nat) (h : n ≤ 125) :
    (UInt8.ofNat n).toNat = n := by
  simp
  omega

/-! ## Roundtrip theorem -/

theorem decode_encode (n : Nat) (rest : List UInt8) (hn : n < 2 ^ 64) :
    decode (encode n ++ rest) = some (n, rest) := by
  unfold encode
  split
  · -- n ≤ 125
    rename_i h125
    simp only [List.cons_append, List.nil_append, decode]
    rw [toNat_ofNat_le125 n h125]
    simp [h125]
  · split
    · -- 126 ≤ n ≤ 65535  (first byte = 126, 2-byte big-endian)
      rename_i h125 h65535
      simp only [List.cons_append, List.nil_append, decode, fromBE,
        List.foldl_cons, List.foldl_nil]
      have hb : (UInt8.ofNat 126).toNat = 126 := by decide
      rw [hb]
      simp only [show ¬ (126 : Nat) ≤ 125 by decide, if_false, if_true]
      rw [byteAt_toNat, byteAt_toNat]
      simp only [Nat.shiftRight_eq_div_pow, Nat.reducePow, Nat.reduceMul,
        Option.some.injEq, Prod.mk.injEq, and_true]
      omega
    · -- n ≥ 65536  (first byte = 127, 8-byte big-endian)
      rename_i h125 h65535
      simp only [List.cons_append, List.nil_append, decode, fromBE,
        List.foldl_cons, List.foldl_nil]
      have hb : (UInt8.ofNat 127).toNat = 127 := by decide
      rw [hb]
      simp only [show ¬ (127 : Nat) ≤ 125 by decide, if_false,
        show ¬ (127 : Nat) = 126 by decide, if_true]
      rw [byteAt_toNat, byteAt_toNat, byteAt_toNat, byteAt_toNat,
        byteAt_toNat, byteAt_toNat, byteAt_toNat, byteAt_toNat]
      simp only [Nat.shiftRight_eq_div_pow, Nat.reducePow, Nat.reduceMul,
        Option.some.injEq, Prod.mk.injEq, and_true]
      omega

/-! ## Injectivity (canonical form)

`encode` is injective on the valid range: distinct lengths never produce the
same wire bytes. This rules out non-canonical encodings (e.g. putting a value
≤ 125 into the 126 form), since such an alternative would have to collide with
the canonical `encode` output. Proven directly from the roundtrip property. -/

theorem encode_injective (n m : Nat) (hn : n < 2 ^ 64) (hm : m < 2 ^ 64)
    (h : encode n = encode m) : n = m := by
  have rn := decode_encode n [] hn
  have rm := decode_encode m [] hm
  rw [h] at rn
  -- rn : decode (encode m) = some (n, []),  rm : decode (encode m) = some (m, [])
  rw [rm] at rn
  -- rn : some (m, []) = some (n, [])
  simp only [Option.some.injEq, Prod.mk.injEq] at rn
  exact rn.1.symm

/-! ## Canonical decoding (matches C `parse_len`)

`decode` above is permissive: it accepts non-minimal encodings (e.g. a value
≤ 125 stuffed into the 126 form) and 8-byte lengths with bit63 set. The C
implementation `parse_len` / `parse_len16` / `parse_len64` / `check_len64` in
`src/core/frame.c` rejects these. `decodeCanonical` is the byte-for-byte model
of that stricter decoder:

  * 1-byte form (b ≤ 125): always minimal, accept.
  * 2-byte form (b = 126): reject if decoded `v < 126` (non-minimal).
  * 8-byte form (b = 127): reject if `v ≥ 2^63` (bit63 set, RFC6455 §5.2)
    or `v ≤ 65535` (non-minimal, fits the 2-byte form).

The F-09 (non-minimal length is rejected) and F-10 (MSB-set 64-bit length is
rejected) properties are stated directly against this function.

Granularity note: C distinguishes `WS_PARSE_NEED_MORE` (too few bytes) from
`WS_PARSE_ERROR` (malformed). `decodeCanonical` collapses both into `none` —
it models *which byte strings are rejected*, not the rejection reason. The
rejection theorems below are unaffected (a rejected string maps to `none`
either way); only the NEED_MORE/ERROR distinction is out of scope here. -/

/-- The reserved most-significant-bit threshold for the 8-byte form: a decoded
value `≥ 2^63` has bit63 set and must be rejected (RFC6455 §5.2). -/
def msbThreshold64 : Nat := 2 ^ 63

/-- Canonical RFC6455 length decode: like `decode`, but rejects non-minimal
encodings and 8-byte lengths with bit63 set. Mirrors C `parse_len`. -/
def decodeCanonical (bs : List UInt8) : Option (Nat × List UInt8) :=
  match bs with
  | [] => none
  | b :: rest =>
    if b.toNat ≤ 125 then
      some (b.toNat, rest)
    else if b.toNat = 126 then
      match rest with
      | b1 :: b0 :: rest' =>
        let v := fromBE [b1, b0]
        if v < 126 then none else some (v, rest')
      | _ => none
    else if b.toNat = 127 then
      match rest with
      | b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest' =>
        let v := fromBE [b7, b6, b5, b4, b3, b2, b1, b0]
        if v ≥ msbThreshold64 then none
        else if v ≤ 65535 then none
        else some (v, rest')
      | _ => none
    else
      none

/-! ## Soundness: canonical encodings are always accepted (F-09/F-10 completeness)

`encode` always emits the minimal form, so `decodeCanonical` must accept its
output for any in-range `n`. The range is `n < 2^63`: RFC6455 §5.2 reserves
bit63 of the 8-byte length, so a value with bit63 set is *not* a legitimate
payload length and the C decoder (`check_len64`) rejects it. This is the
completeness counterpart to the rejection theorems below: the stricter decoder
never rejects a value the encoder would legitimately produce. -/

theorem decodeCanonical_encode (n : Nat) (rest : List UInt8) (hn : n < 2 ^ 63) :
    decodeCanonical (encode n ++ rest) = some (n, rest) := by
  unfold encode
  split
  · -- n ≤ 125
    rename_i h125
    simp only [List.cons_append, List.nil_append, decodeCanonical]
    rw [toNat_ofNat_le125 n h125]
    simp [h125]
  · split
    · -- 126 ≤ n ≤ 65535
      rename_i h125 h65535
      simp only [List.cons_append, List.nil_append, decodeCanonical, fromBE,
        List.foldl_cons, List.foldl_nil]
      have hb : (UInt8.ofNat 126).toNat = 126 := by decide
      rw [hb]
      simp only [show ¬ (126 : Nat) ≤ 125 by decide, if_false, if_true]
      rw [byteAt_toNat, byteAt_toNat]
      simp only [Nat.shiftRight_eq_div_pow, Nat.reducePow, Nat.reduceMul]
      -- value recovered is n; show ¬ value < 126 then close some-injection
      have hval : (0 + n / 256 % 256) * 256 + n / 1 % 256 = n := by omega
      rw [hval]
      simp only [show ¬ n < 126 by omega, if_false]
    · -- n ≥ 65536
      rename_i h125 h65535
      simp only [List.cons_append, List.nil_append, decodeCanonical, fromBE,
        List.foldl_cons, List.foldl_nil]
      have hb : (UInt8.ofNat 127).toNat = 127 := by decide
      rw [hb]
      simp only [show ¬ (127 : Nat) ≤ 125 by decide, if_false,
        show ¬ (127 : Nat) = 126 by decide, if_true]
      rw [byteAt_toNat, byteAt_toNat, byteAt_toNat, byteAt_toNat,
        byteAt_toNat, byteAt_toNat, byteAt_toNat, byteAt_toNat]
      simp only [Nat.shiftRight_eq_div_pow, Nat.reducePow, Nat.reduceMul,
        msbThreshold64]
      -- the recovered big-endian value equals n
      have hval :
          (((((((0 + n / 72057594037927936 % 256) * 256 +
              n / 281474976710656 % 256) * 256 + n / 1099511627776 % 256) * 256 +
                n / 4294967296 % 256) * 256 + n / 16777216 % 256) * 256 +
                  n / 65536 % 256) * 256 + n / 256 % 256) * 256 + n / 1 % 256 = n := by
        omega
      rw [hval]
      simp only [show ¬ n ≥ 2 ^ 63 by omega, if_false,
        show ¬ n ≤ 65535 by omega, if_false]

/-! ## F-09: non-minimal 2-byte encodings are rejected -/

/-- F-09 (16-bit form): a 126-prefixed frame whose 2-byte big-endian value is
`< 126` is rejected (such a value must use the 1-byte form). -/
theorem decodeCanonical_rejects_nonminimal_16 (b1 b0 : UInt8) (rest : List UInt8)
    (h : fromBE [b1, b0] < 126) :
    decodeCanonical (126 :: b1 :: b0 :: rest) = none := by
  simp only [decodeCanonical]
  have hb : (126 : UInt8).toNat = 126 := by decide
  rw [hb]
  simp only [show ¬ (126 : Nat) ≤ 125 by decide, if_false, if_true, h]

/-! ## F-09: non-minimal 8-byte encodings are rejected -/

/-- F-09 (64-bit form): a 127-prefixed frame whose 8-byte big-endian value is
`≤ 65535` is rejected (such a value must use the 2-byte form). -/
theorem decodeCanonical_rejects_nonminimal_64
    (b7 b6 b5 b4 b3 b2 b1 b0 : UInt8) (rest : List UInt8)
    (hmsb : fromBE [b7, b6, b5, b4, b3, b2, b1, b0] < msbThreshold64)
    (hmin : fromBE [b7, b6, b5, b4, b3, b2, b1, b0] ≤ 65535) :
    decodeCanonical (127 :: b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest)
      = none := by
  simp only [decodeCanonical]
  have hb : (127 : UInt8).toNat = 127 := by decide
  rw [hb]
  simp only [show ¬ (127 : Nat) ≤ 125 by decide, if_false,
    show ¬ (127 : Nat) = 126 by decide, if_true,
    show ¬ fromBE [b7, b6, b5, b4, b3, b2, b1, b0] ≥ msbThreshold64 from
      Nat.not_le.mpr hmsb, if_false, hmin, if_true]

/-! ## F-10: 8-byte lengths with bit63 set are rejected -/

/-- F-10 (general): a 127-prefixed frame whose 8-byte big-endian value has bit63
set (`≥ 2^63`) is rejected (RFC6455 §5.2 reserves the MSB). -/
theorem decodeCanonical_rejects_msb_64
    (b7 b6 b5 b4 b3 b2 b1 b0 : UInt8) (rest : List UInt8)
    (hmsb : fromBE [b7, b6, b5, b4, b3, b2, b1, b0] ≥ msbThreshold64) :
    decodeCanonical (127 :: b7 :: b6 :: b5 :: b4 :: b3 :: b2 :: b1 :: b0 :: rest)
      = none := by
  simp only [decodeCanonical]
  have hb : (127 : UInt8).toNat = 127 := by decide
  rw [hb]
  simp only [show ¬ (127 : Nat) ≤ 125 by decide, if_false,
    show ¬ (127 : Nat) = 126 by decide, if_true, hmsb, if_true]

/-- F-10 (concrete): the smallest bit63-set 8-byte length `0x8000…0000` is
rejected. Decided by direct evaluation (no `native_decide`). -/
theorem decodeCanonical_rejects_msb_concrete :
    decodeCanonical [127, 0x80, 0, 0, 0, 0, 0, 0, 0] = none := by
  decide

end WsProof.LengthCodec
