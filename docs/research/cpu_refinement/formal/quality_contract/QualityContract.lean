import Lean

/- QPC-04E: scalar laws only. No geometry, floating-point or runtime model.
   Scalar is an abstract linearly ordered additive domain. These are ordinary
   order/translation laws, not assumptions of the quality claims below.
   In particular no Nat quantization of squared errors is used. -/
namespace QualityContract

structure Scalar (α : Type) where
  le : α → α → Prop
  decideLe : DecidableRel le
  refl : ∀ a, le a a
  trans : ∀ {a b c}, le a b → le b c → le a c
  antisymm : ∀ {a b}, le a b → le b a → a = b
  total : ∀ a b, le a b ∨ le b a
  zero : α
  add : α → α → α
  addZero : ∀ a, add a zero = a
  zeroAdd : ∀ a, add zero a = a
  addMono : ∀ {a b c d}, le a b → le c d → le (add a c) (add b d)
  sub : α → α → α
  subSelf : ∀ a, sub a a = zero
  subOrder : ∀ a b c, le (sub a c) (sub b c) ↔ le a b

namespace Scalar
variable {α : Type} (s : Scalar α)
local instance : DecidableRel s.le := s.decideLe

def cap (x c : α) : α := if s.le x c then c else x
def excess (x c : α) : α := s.cap (s.sub x c) s.zero

theorem cap_le {x c z : α} : s.le (s.cap x c) z ↔ s.le x z ∧ s.le c z := by
  unfold cap
  split
  · rename_i h
    exact ⟨fun hz => ⟨s.trans h hz, hz⟩, fun h => h.2⟩
  · rename_i h
    have hc : s.le c x := (s.total x c).resolve_left h
    exact ⟨fun hz => ⟨hz, s.trans hc hz⟩, fun h => h.1⟩

theorem le_cap_left (x c : α) : s.le x (s.cap x c) := by
  unfold cap
  split
  · assumption
  · exact s.refl x

theorem le_cap_right (x c : α) : s.le c (s.cap x c) := by
  unfold cap
  split
  · exact s.refl c
  · exact (s.total x c).resolve_left ‹¬s.le x c›

theorem cap_order {x y c : α} :
    s.le y (s.cap x c) ↔ s.le (s.cap y c) (s.cap x c) := by
  exact ⟨fun h => (cap_le s).mpr ⟨h, le_cap_right s x c⟩,
    fun h => s.trans (le_cap_left s y c) h⟩

theorem excess_translate (x c : α) : s.excess x c = s.sub (s.cap x c) c := by
  have h : s.le (s.sub x c) s.zero ↔ s.le x c := by
    rw [←s.subSelf c]
    exact s.subOrder x c c
  by_cases hx : s.le x c
  · simp [excess, cap, hx, h.mpr hx, s.subSelf]
  · have hn : ¬s.le (s.sub x c) s.zero := fun p => hx (h.mp p)
    simp [excess, cap, hx, hn]

-- QC-01: pointwise allowance is exactly non-increase of positive excess.
theorem pointwise_iff_excess (x y c : α) :
    s.le y (s.cap x c) ↔ s.le (s.excess y c) (s.excess x c) := by
  rw [excess_translate s y c, excess_translate s x c, s.subOrder]
  exact cap_order s

theorem cap_idempotent (x c : α) : s.cap (s.cap x c) c = s.cap x c := by
  apply s.antisymm
  · exact (cap_le s).mpr ⟨s.refl _, le_cap_right s x c⟩
  · exact le_cap_left s _ c

-- QC-02: a fixed cap cannot grow by accumulation across transactions.
theorem sequence_envelope (u : Nat → α) (c : α)
    (step : ∀ t, s.le (u (t+1)) (s.cap (u t) c)) :
    ∀ t, s.le (u t) (s.cap (u 0) c) := by
  intro t
  induction t with
  | zero => exact le_cap_left s _ _
  | succ t ih =>
    exact s.trans (step t) ((cap_le s).mpr ⟨ih, le_cap_right s _ _⟩)

def totalCost : List α → α
  | [] => s.zero
  | x :: xs => s.add x (totalCost xs)

theorem sum_mono (pairs : List (α × α))
    (h : ∀ p ∈ pairs, s.le p.1 p.2) :
    s.le (s.totalCost (pairs.map Prod.fst)) (s.totalCost (pairs.map Prod.snd)) := by
  induction pairs with
  | nil => exact s.refl _
  | cons p ps ih =>
    apply s.addMono
    · exact h p (by simp)
    · exact ih (fun q hq => h q (by simp [hq]))

theorem sum_nonnegative (xs : List α) (h : ∀ x ∈ xs, s.le s.zero x) :
    s.le s.zero (s.totalCost xs) := by
  induction xs with
  | nil => exact s.refl _
  | cons x xs ih =>
    have hh := s.addMono (h x (by simp)) (ih (fun y hy => h y (by simp [hy])))
    simpa [totalCost, s.zeroAdd] using hh

theorem sum_zero_iff (xs : List α) (h : ∀ x ∈ xs, s.le s.zero x) :
    s.totalCost xs = s.zero ↔ ∀ x ∈ xs, x = s.zero := by
  induction xs with
  | nil => simp [totalCost]
  | cons x xs ih =>
    have ht : ∀ y ∈ xs, s.le s.zero y := fun y hy => h y (by simp [hy])
    constructor
    · intro hz
      have hx := s.addMono (s.refl x) (sum_nonnegative s xs ht)
      have hs := s.addMono (h x (by simp)) (s.refl (s.totalCost xs))
      change s.add x (s.totalCost xs) = s.zero at hz
      have ex : x = s.zero := s.antisymm (by simpa [s.addZero, hz] using hx) (h x (by simp))
      have es : s.totalCost xs = s.zero := s.antisymm (by simpa [s.zeroAdd, hz] using hs) (sum_nonnegative s xs ht)
      intro y hy
      rcases List.mem_cons.mp hy with he | he
      · simpa [he] using ex
      · exact (ih ht).mp es y he
    · intro hz
      have ex := hz x (by simp)
      have es := (ih ht).mpr (fun y hy => hz y (by simp [hy]))
      simp [totalCost, ex, es, s.zeroAdd]

theorem excess_zero_iff (x c : α) : s.excess x c = s.zero ↔ s.le x c := by
  constructor
  · intro h
    have hx := le_cap_left s (s.sub x c) s.zero
    change s.le (s.sub x c) (s.excess x c) at hx
    rw [h, ←s.subSelf c] at hx
    exact (s.subOrder x c c).mp hx
  · intro h
    rw [excess_translate s x c]
    simp [cap, h, s.subSelf]

-- QC-03: finite sampled potential, not a continuous-surface theorem.
theorem potential_zero_iff (xs : List α) (c : α) :
    s.totalCost (xs.map (fun x => s.excess x c)) = s.zero ↔ ∀ x ∈ xs, s.le x c := by
  rw [sum_zero_iff s _ (by
    intro y hy
    obtain ⟨x, _, rfl⟩ := List.mem_map.mp hy
    exact le_cap_right s _ _)]
  constructor
  · intro h x hx
    exact (excess_zero_iff s x c).mp (h _ (List.mem_map.mpr ⟨x, hx, rfl⟩))
  · intro h y hy
    obtain ⟨x, hx, rfl⟩ := List.mem_map.mp hy
    exact (excess_zero_iff s x c).mpr (h x hx)

theorem potential_nonincrease (pairs : List (α × α)) (c : α)
    (h : ∀ p ∈ pairs, s.le p.2 (s.cap p.1 c)) :
    s.le (s.totalCost (pairs.map (fun p => s.excess p.2 c)))
         (s.totalCost (pairs.map (fun p => s.excess p.1 c))) := by
  have hh := sum_mono s (pairs.map (fun p => (s.excess p.2 c, s.excess p.1 c))) (by
    intro q hq
    obtain ⟨p, hp, rfl⟩ := List.mem_map.mp hq
    exact (pointwise_iff_excess s p.1 p.2 c).mp (h p hp))
  simpa [List.map_map] using hh

-- The maximum consequence is a pointwise common upper bound, so empty Q
-- needs no arbitrary maximum convention.
theorem common_maximum_bound (x y c upper : α)
    (old : s.le x upper) (step : s.le y (s.cap x c)) :
    s.le y (s.cap upper c) :=
  s.trans step ((cap_le s).mpr ⟨s.trans old (le_cap_left s _ _), le_cap_right s _ _⟩)

#print axioms pointwise_iff_excess
#print axioms sequence_envelope
#print axioms potential_nonincrease
#print axioms potential_zero_iff
#print axioms common_maximum_bound
end Scalar
end QualityContract
