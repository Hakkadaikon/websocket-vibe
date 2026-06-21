/-!
# RFC6455 Section 5.3 — Frame masking

The masking transformation is defined byte-wise as

  transformed[i] = original[i] XOR masking-key[i % 4]

We model the masking key abstractly as a function `key : Nat → UInt8`.
A concrete 4-byte key `k : Array UInt8` corresponds to `fun i => k[i % 4]!`,
so the C implementation must compute `transformed[i] = data[i] XOR k[i % 4]`.

`mask` walks the payload from a starting index, XOR-ing each byte with the
key sampled at that index. The public entry point `maskList` starts at 0.

Two properties are proven without `sorry`:

* `mask_involution` / `maskList_involution`: masking twice with the same key
  restores the original payload.
* `mask_length` / `maskList_length`: masking preserves payload length.
-/

namespace WsProof.Masking

/-- XOR self-inverse on `UInt8`: `x ^^^ y ^^^ y = x`.
Proven via the `BitVec` model where `xor` is associative and self-cancelling. -/
theorem UInt8.xor_xor_cancel (x y : UInt8) : x ^^^ y ^^^ y = x := by
  have h : (x ^^^ y ^^^ y).toBitVec = x.toBitVec := by
    rw [UInt8.toBitVec_xor, UInt8.toBitVec_xor, BitVec.xor_assoc,
      BitVec.xor_self, BitVec.xor_zero]
  exact UInt8.toBitVec_inj.mp h

/-- Mask `data` starting at payload index `i`, XOR-ing each byte with `key`
sampled at its absolute index. -/
def mask (key : Nat → UInt8) (i : Nat) : List UInt8 → List UInt8
  | [] => []
  | b :: rest => (b ^^^ key i) :: mask key (i + 1) rest

/-- Public entry point: mask the whole payload starting at index 0. -/
def maskList (key : Nat → UInt8) (data : List UInt8) : List UInt8 :=
  mask key 0 data

/-- Masking preserves length (indexed form). -/
theorem mask_length (key : Nat → UInt8) (i : Nat) (data : List UInt8) :
    (mask key i data).length = data.length := by
  induction data generalizing i with
  | nil => rfl
  | cons b rest ih => simp [mask, ih]

/-- Masking preserves length. -/
theorem maskList_length (key : Nat → UInt8) (data : List UInt8) :
    (maskList key data).length = data.length :=
  mask_length key 0 data

/-- Masking is an involution (indexed form): same key, same start index. -/
theorem mask_involution (key : Nat → UInt8) (i : Nat) (data : List UInt8) :
    mask key i (mask key i data) = data := by
  induction data generalizing i with
  | nil => rfl
  | cons b rest ih => simp [mask, UInt8.xor_xor_cancel, ih]

/-- Masking twice with the same key restores the original payload. -/
theorem maskList_involution (key : Nat → UInt8) (data : List UInt8) :
    maskList key (maskList key data) = data :=
  mask_involution key 0 data

end WsProof.Masking
