/-!
# RFC3629 / RFC6455 — Well-formed UTF-8 validation

RFC6455 (§8.1) requires that text frames (opcode 0x1) and the *reason* part of
Close frames carry **well-formed UTF-8** (this is what Autobahn test suite 6.x
exercises). This module formalises the soundness (and completeness) of a UTF-8
validator restricted to the RFC3629 byte ranges.

## Byte-range specification (RFC3629)

A well-formed UTF-8 string is a concatenation of code-point encodings, each of
which is one of:

* **1 byte**  `00..7F`
* **2 bytes** `C2..DF` `80..BF`                       (overlong `C0/C1` rejected)
* **3 bytes** `E0` `A0..BF` `80..BF`
              `E1..EC` `80..BF` `80..BF`
              `ED` `80..9F` `80..BF`                   (surrogates rejected)
              `EE..EF` `80..BF` `80..BF`
* **4 bytes** `F0` `90..BF` `80..BF` `80..BF`
              `F1..F3` `80..BF` `80..BF` `80..BF`
              `F4` `80..8F` `80..BF` `80..BF`          (> U+10FFFF rejected)

## What is proven

* `WellFormed : List UInt8 → Prop` — the inductive specification above.
* `validate : List UInt8 → Bool` — an executable recursive validator whose
  branching matches the C implementation (see `step` below).
* `validate_sound`     : `validate bs = true → WellFormed bs`.
* `validate_complete`  : `WellFormed bs → validate bs = true`.
* `validate_correct`   : `validate bs = true ↔ WellFormed bs`.

All theorems are `sorry`-free.

## C implementation contract

The validator is a byte-driven recursion. For the leading byte `b` of the
remaining input it dispatches on `b` and then checks the required number of
continuation bytes, each of which must lie in `80..BF`, with the first
continuation byte further restricted for `E0`, `ED`, `F0`, `F4`:

```
b in 00..7F        -> accept 1 byte
b in C2..DF        -> need c1 in 80..BF                         (2 bytes)
b == E0            -> need c1 in A0..BF, c2 in 80..BF           (3 bytes)
b in E1..EC        -> need c1 in 80..BF, c2 in 80..BF          (3 bytes)
b == ED            -> need c1 in 80..9F, c2 in 80..BF          (3 bytes)
b in EE..EF        -> need c1 in 80..BF, c2 in 80..BF          (3 bytes)
b == F0            -> need c1 in 90..BF, c2,c3 in 80..BF       (4 bytes)
b in F1..F3        -> need c1 in 80..BF, c2,c3 in 80..BF       (4 bytes)
b == F4            -> need c1 in 80..8F, c2,c3 in 80..BF       (4 bytes)
otherwise          -> reject
```
-/

namespace WsProof.Utf8

/-- A continuation byte: `0x80..0xBF`. -/
def isCont (b : UInt8) : Bool := 0x80 ≤ b && b ≤ 0xBF

/-!
## Specification: `WellFormed`

The predicate is built one code point at a time. Each constructor consumes the
1–4 bytes of a single code-point encoding and prepends them to a well-formed
tail.  `nil` is well-formed (the empty string).
-/

inductive WellFormed : List UInt8 → Prop where
  | nil : WellFormed []
  | one (b : UInt8) (rest : List UInt8)
      (h : b ≤ 0x7F) (ht : WellFormed rest) :
      WellFormed (b :: rest)
  | two (b c1 : UInt8) (rest : List UInt8)
      (hb : 0xC2 ≤ b ∧ b ≤ 0xDF)
      (hc1 : isCont c1 = true)
      (ht : WellFormed rest) :
      WellFormed (b :: c1 :: rest)
  -- 3-byte forms
  | threeE0 (c1 c2 : UInt8) (rest : List UInt8)
      (hc1 : 0xA0 ≤ c1 ∧ c1 ≤ 0xBF)
      (hc2 : isCont c2 = true)
      (ht : WellFormed rest) :
      WellFormed (0xE0 :: c1 :: c2 :: rest)
  | threeMid (b c1 c2 : UInt8) (rest : List UInt8)
      (hb : 0xE1 ≤ b ∧ b ≤ 0xEC)
      (hc1 : isCont c1 = true)
      (hc2 : isCont c2 = true)
      (ht : WellFormed rest) :
      WellFormed (b :: c1 :: c2 :: rest)
  | threeED (c1 c2 : UInt8) (rest : List UInt8)
      (hc1 : 0x80 ≤ c1 ∧ c1 ≤ 0x9F)
      (hc2 : isCont c2 = true)
      (ht : WellFormed rest) :
      WellFormed (0xED :: c1 :: c2 :: rest)
  | threeEE (b c1 c2 : UInt8) (rest : List UInt8)
      (hb : 0xEE ≤ b ∧ b ≤ 0xEF)
      (hc1 : isCont c1 = true)
      (hc2 : isCont c2 = true)
      (ht : WellFormed rest) :
      WellFormed (b :: c1 :: c2 :: rest)
  -- 4-byte forms
  | fourF0 (c1 c2 c3 : UInt8) (rest : List UInt8)
      (hc1 : 0x90 ≤ c1 ∧ c1 ≤ 0xBF)
      (hc2 : isCont c2 = true)
      (hc3 : isCont c3 = true)
      (ht : WellFormed rest) :
      WellFormed (0xF0 :: c1 :: c2 :: c3 :: rest)
  | fourMid (b c1 c2 c3 : UInt8) (rest : List UInt8)
      (hb : 0xF1 ≤ b ∧ b ≤ 0xF3)
      (hc1 : isCont c1 = true)
      (hc2 : isCont c2 = true)
      (hc3 : isCont c3 = true)
      (ht : WellFormed rest) :
      WellFormed (b :: c1 :: c2 :: c3 :: rest)
  | fourF4 (c1 c2 c3 : UInt8) (rest : List UInt8)
      (hc1 : 0x80 ≤ c1 ∧ c1 ≤ 0x8F)
      (hc2 : isCont c2 = true)
      (hc3 : isCont c3 = true)
      (ht : WellFormed rest) :
      WellFormed (0xF4 :: c1 :: c2 :: c3 :: rest)

/-!
## Executable validator

`validate` recurses on the structure of the byte list. To get a clean
structural recursion that Lean accepts, we match the leading byte plus enough
continuation bytes at once, recursing on the unconsumed tail.
-/

def validate : List UInt8 → Bool
  | [] => true
  -- 1-byte
  | b :: rest =>
    if b ≤ 0x7F then validate rest
    -- 2-byte
    else if 0xC2 ≤ b && b ≤ 0xDF then
      match rest with
      | c1 :: rest2 => isCont c1 && validate rest2
      | [] => false
    -- 3-byte
    else if b == 0xE0 then
      match rest with
      | c1 :: c2 :: rest3 =>
        (0xA0 ≤ c1 && c1 ≤ 0xBF) && isCont c2 && validate rest3
      | _ => false
    else if 0xE1 ≤ b && b ≤ 0xEC then
      match rest with
      | c1 :: c2 :: rest3 => isCont c1 && isCont c2 && validate rest3
      | _ => false
    else if b == 0xED then
      match rest with
      | c1 :: c2 :: rest3 =>
        (0x80 ≤ c1 && c1 ≤ 0x9F) && isCont c2 && validate rest3
      | _ => false
    else if 0xEE ≤ b && b ≤ 0xEF then
      match rest with
      | c1 :: c2 :: rest3 => isCont c1 && isCont c2 && validate rest3
      | _ => false
    -- 4-byte
    else if b == 0xF0 then
      match rest with
      | c1 :: c2 :: c3 :: rest4 =>
        (0x90 ≤ c1 && c1 ≤ 0xBF) && isCont c2 && isCont c3 && validate rest4
      | _ => false
    else if 0xF1 ≤ b && b ≤ 0xF3 then
      match rest with
      | c1 :: c2 :: c3 :: rest4 =>
        isCont c1 && isCont c2 && isCont c3 && validate rest4
      | _ => false
    else if b == 0xF4 then
      match rest with
      | c1 :: c2 :: c3 :: rest4 =>
        (0x80 ≤ c1 && c1 ≤ 0x8F) && isCont c2 && isCont c3 && validate rest4
      | _ => false
    else false

/-!
## Soundness

`validate bs = true → WellFormed bs`. We use strong induction on the length of
the list so the recursive calls on the tail can be discharged.
-/

theorem validate_sound : ∀ bs : List UInt8, validate bs = true → WellFormed bs := by
  intro bs
  induction bs using validate.induct with
  | case1 => intro _; exact WellFormed.nil
  | case2 b rest hb ih =>
    intro h
    unfold validate at h
    simp only [hb, if_true] at h
    exact WellFormed.one b rest hb (ih h)
  | case3 b hb1 hb c1 rest2 ih =>
    intro h
    have hb' : 0xC2 ≤ b ∧ b ≤ 0xDF := by simp only [Bool.and_eq_true, decide_eq_true_eq] at hb; exact hb
    unfold validate at h
    simp only [hb1, hb, reduceIte, Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.two b c1 rest2 hb' h.1 (ih h.2)
  | case4 b hb1 hb =>
    intro h; unfold validate at h
    simp only [hb1, hb, reduceCtorEq, reduceIte] at h
  | case5 b hb1 hb2 hb c1 c2 rest3 ih =>
    intro h
    have hbe : b = 0xE0 := by simpa using hb
    subst hbe
    unfold validate at h
    simp +decide only [reduceIte, Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.threeE0 c1 c2 rest3 h.1.1 h.1.2 (ih h.2)
  | case6 b rest hb1 hb2 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, rest3⟩⟩
    · unfold validate at h; simp only [hb1, hb2, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 rest3)
  | case7 b hb1 hb2 hb3 hb c1 c2 rest3 ih =>
    intro h
    have hb' : 0xE1 ≤ b ∧ b ≤ 0xEC := by simp only [Bool.and_eq_true, decide_eq_true_eq] at hb; exact hb
    unfold validate at h
    simp only [hb1, hb2, hb3, hb, reduceCtorEq, reduceIte, Bool.and_eq_true,
      decide_eq_true_eq] at h
    exact WellFormed.threeMid b c1 c2 rest3 hb' h.1.1 h.1.2 (ih h.2)
  | case8 b rest hb1 hb2 hb3 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, rest3⟩⟩
    · unfold validate at h; simp only [hb1, hb2, hb3, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb3, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 rest3)
  | case9 b hb1 hb2 hb3 hb4 hb c1 c2 rest3 ih =>
    intro h
    have hbe : b = 0xED := by simpa using hb
    subst hbe
    unfold validate at h
    simp +decide only [reduceIte, Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.threeED c1 c2 rest3 h.1.1 h.1.2 (ih h.2)
  | case10 b rest hb1 hb2 hb3 hb4 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, rest3⟩⟩
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 rest3)
  | case11 b hb1 hb2 hb3 hb4 hb5 hb c1 c2 rest3 ih =>
    intro h
    have hb' : 0xEE ≤ b ∧ b ≤ 0xEF := by simp only [Bool.and_eq_true, decide_eq_true_eq] at hb; exact hb
    unfold validate at h
    simp only [hb1, hb2, hb3, hb4, hb5, hb, reduceCtorEq, reduceIte, Bool.and_eq_true,
      decide_eq_true_eq] at h
    exact WellFormed.threeEE b c1 c2 rest3 hb' h.1.1 h.1.2 (ih h.2)
  | case12 b rest hb1 hb2 hb3 hb4 hb5 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, rest3⟩⟩
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb5, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb5, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 rest3)
  | case13 b hb1 hb2 hb3 hb4 hb5 hb6 hb c1 c2 c3 rest4 ih =>
    intro h
    have hbe : b = 0xF0 := by simpa using hb
    subst hbe
    unfold validate at h
    simp +decide only [reduceIte, Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.fourF0 c1 c2 c3 rest4 h.1.1.1 h.1.1.2 h.1.2 (ih h.2)
  | case14 b rest hb1 hb2 hb3 hb4 hb5 hb6 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, _ | ⟨c3, rest4⟩⟩⟩
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h; simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 c3 rest4)
  | case15 b hb1 hb2 hb3 hb4 hb5 hb6 hb7 hb c1 c2 c3 rest4 ih =>
    intro h
    have hb' : 0xF1 ≤ b ∧ b ≤ 0xF3 := by simp only [Bool.and_eq_true, decide_eq_true_eq] at hb; exact hb
    unfold validate at h
    simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb, reduceCtorEq, reduceIte,
      Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.fourMid b c1 c2 c3 rest4 hb' h.1.1.1 h.1.1.2 h.1.2 (ih h.2)
  | case16 b rest hb1 hb2 hb3 hb4 hb5 hb6 hb7 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, _ | ⟨c3, rest4⟩⟩⟩
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 c3 rest4)
  | case17 b hb1 hb2 hb3 hb4 hb5 hb6 hb7 hb8 hb c1 c2 c3 rest4 ih =>
    intro h
    have hbe : b = 0xF4 := by simpa using hb
    subst hbe
    unfold validate at h
    simp +decide only [reduceIte, Bool.and_eq_true, decide_eq_true_eq] at h
    exact WellFormed.fourF4 c1 c2 c3 rest4 h.1.1.1 h.1.1.2 h.1.2 (ih h.2)
  | case18 b rest hb1 hb2 hb3 hb4 hb5 hb6 hb7 hb8 hb hf =>
    intro h
    rcases rest with _ | ⟨c1, _ | ⟨c2, _ | ⟨c3, rest4⟩⟩⟩
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb8, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb8, hb, reduceCtorEq, reduceIte] at h
    · unfold validate at h
      simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb8, hb, reduceCtorEq, reduceIte] at h
    · exact absurd rfl (hf c1 c2 c3 rest4)
  | case19 b rest hb1 hb2 hb3 hb4 hb5 hb6 hb7 hb8 hb9 =>
    intro h; unfold validate at h
    simp only [hb1, hb2, hb3, hb4, hb5, hb6, hb7, hb8, hb9, reduceCtorEq, reduceIte] at h

/-!
## Completeness

`WellFormed bs → validate bs = true`. Structural induction on the derivation:
each constructor pins down the leading byte's range, which forces the matching
branch of `validate`. UInt8 order facts are discharged by reducing to `Nat`
via `UInt8.toNat` and `omega`.
-/

/-- Prove a single `validate` branch condition (a `≤` / `&&` / `==` fact on a
leading byte) from the `b.toNat` bounds already in context. -/
local macro "ule" : tactic =>
  `(tactic| (rw [UInt8.le_iff_toNat_le]; simp only [UInt8.toNat_ofNat]; omega))
local macro "bcond" : tactic => `(tactic| (
  first
    | (rw [UInt8.le_iff_toNat_le]; simp only [UInt8.toNat_ofNat]; omega)
    | (simp only [Bool.and_eq_true, decide_eq_true_eq]; refine ⟨?_, ?_⟩ <;> ule)
    | (simp only [Bool.and_eq_false_iff, decide_eq_false_iff_not,
        UInt8.le_iff_toNat_le, UInt8.toNat_ofNat]; omega)
    | (simp only [beq_eq_false_iff_ne, ne_eq, ← UInt8.toNat_inj, UInt8.toNat_ofNat]; omega)))

theorem validate_complete : ∀ bs : List UInt8, WellFormed bs → validate bs = true := by
  intro bs hwf
  induction hwf with
  | nil => rfl
  | one b rest hb _ ih =>
    unfold validate; simp only [hb, if_true]; exact ih
  | two b c1 rest hb hc1 _ ih =>
    obtain ⟨h1, h2⟩ := hb
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at h1 h2
    unfold validate
    simp only [show ¬ b ≤ 0x7F by bcond, if_false,
      show (0xC2 ≤ b && b ≤ 0xDF) = true by bcond, if_true, hc1, ih, Bool.and_self]
  | threeE0 c1 c2 rest hc1 hc2 _ ih =>
    obtain ⟨ha, hb⟩ := hc1
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at ha hb
    unfold validate
    simp only [show ¬ (0xE0:UInt8) ≤ 0x7F by decide, if_false,
      show ((0xC2:UInt8) ≤ 0xE0 && (0xE0:UInt8) ≤ 0xDF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xE0:UInt8) == 0xE0) = true by decide, if_true,
      show (0xA0 ≤ c1 && c1 ≤ 0xBF) = true by bcond, hc2, ih, Bool.and_self, Bool.and_true]
  | threeMid b c1 c2 rest hb hc1 hc2 _ ih =>
    obtain ⟨h1, h2⟩ := hb
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at h1 h2
    unfold validate
    simp only [show ¬ b ≤ 0x7F by bcond, if_false,
      show (0xC2 ≤ b && b ≤ 0xDF) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xE0) = false by bcond, if_false,
      show (0xE1 ≤ b && b ≤ 0xEC) = true by bcond, if_true,
      hc1, hc2, ih, Bool.and_self, Bool.and_true]
  | threeED c1 c2 rest hc1 hc2 _ ih =>
    obtain ⟨ha, hb⟩ := hc1
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at ha hb
    unfold validate
    simp only [show ¬ (0xED:UInt8) ≤ 0x7F by decide, if_false,
      show ((0xC2:UInt8) ≤ 0xED && (0xED:UInt8) ≤ 0xDF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xED:UInt8) == 0xE0) = false by decide, if_false,
      show ((0xE1:UInt8) ≤ 0xED && (0xED:UInt8) ≤ 0xEC) = false by decide, Bool.false_eq_true,
      if_false, show ((0xED:UInt8) == 0xED) = true by decide, if_true,
      show (0x80 ≤ c1 && c1 ≤ 0x9F) = true by bcond, hc2, ih, Bool.and_self, Bool.and_true]
  | threeEE b c1 c2 rest hb hc1 hc2 _ ih =>
    obtain ⟨h1, h2⟩ := hb
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at h1 h2
    unfold validate
    simp only [show ¬ b ≤ 0x7F by bcond, if_false,
      show (0xC2 ≤ b && b ≤ 0xDF) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xE0) = false by bcond, if_false,
      show (0xE1 ≤ b && b ≤ 0xEC) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xED) = false by bcond, if_false,
      show (0xEE ≤ b && b ≤ 0xEF) = true by bcond, if_true,
      hc1, hc2, ih, Bool.and_self, Bool.and_true]
  | fourF0 c1 c2 c3 rest hc1 hc2 hc3 _ ih =>
    obtain ⟨ha, hb⟩ := hc1
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at ha hb
    unfold validate
    simp only [show ¬ (0xF0:UInt8) ≤ 0x7F by decide, if_false,
      show ((0xC2:UInt8) ≤ 0xF0 && (0xF0:UInt8) ≤ 0xDF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF0:UInt8) == 0xE0) = false by decide, if_false,
      show ((0xE1:UInt8) ≤ 0xF0 && (0xF0:UInt8) ≤ 0xEC) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF0:UInt8) == 0xED) = false by decide, if_false,
      show ((0xEE:UInt8) ≤ 0xF0 && (0xF0:UInt8) ≤ 0xEF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF0:UInt8) == 0xF0) = true by decide, if_true,
      show (0x90 ≤ c1 && c1 ≤ 0xBF) = true by bcond, hc2, hc3, ih, Bool.and_self, Bool.and_true]
  | fourMid b c1 c2 c3 rest hb hc1 hc2 hc3 _ ih =>
    obtain ⟨h1, h2⟩ := hb
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at h1 h2
    unfold validate
    simp only [show ¬ b ≤ 0x7F by bcond, if_false,
      show (0xC2 ≤ b && b ≤ 0xDF) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xE0) = false by bcond, if_false,
      show (0xE1 ≤ b && b ≤ 0xEC) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xED) = false by bcond, if_false,
      show (0xEE ≤ b && b ≤ 0xEF) = false by bcond, Bool.false_eq_true, if_false,
      show (b == 0xF0) = false by bcond, if_false,
      show (0xF1 ≤ b && b ≤ 0xF3) = true by bcond, if_true,
      hc1, hc2, hc3, ih, Bool.and_self, Bool.and_true]
  | fourF4 c1 c2 c3 rest hc1 hc2 hc3 _ ih =>
    obtain ⟨ha, hb⟩ := hc1
    simp only [UInt8.le_iff_toNat_le, UInt8.toNat_ofNat] at ha hb
    unfold validate
    simp only [show ¬ (0xF4:UInt8) ≤ 0x7F by decide, if_false,
      show ((0xC2:UInt8) ≤ 0xF4 && (0xF4:UInt8) ≤ 0xDF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF4:UInt8) == 0xE0) = false by decide, if_false,
      show ((0xE1:UInt8) ≤ 0xF4 && (0xF4:UInt8) ≤ 0xEC) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF4:UInt8) == 0xED) = false by decide, if_false,
      show ((0xEE:UInt8) ≤ 0xF4 && (0xF4:UInt8) ≤ 0xEF) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF4:UInt8) == 0xF0) = false by decide, if_false,
      show ((0xF1:UInt8) ≤ 0xF4 && (0xF4:UInt8) ≤ 0xF3) = false by decide, Bool.false_eq_true,
      if_false, show ((0xF4:UInt8) == 0xF4) = true by decide, if_true,
      show (0x80 ≤ c1 && c1 ≤ 0x8F) = true by bcond, hc2, hc3, ih, Bool.and_self, Bool.and_true]

/-- The validator exactly characterises well-formed UTF-8. -/
theorem validate_correct (bs : List UInt8) : validate bs = true ↔ WellFormed bs :=
  ⟨validate_sound bs, validate_complete bs⟩

end WsProof.Utf8
