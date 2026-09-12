import Closure

namespace RoamThreshold

attribute [local instance] Classical.propDecidable

-- 数值只表示有限误差值的次序，不能据此声称已建立像素误差上界
structure Hierarchy (α : Type) where
  parent : α → Option α
  depth : α → Nat
  priority : α → Nat
  parent_decreases : ∀ t p, parent t = some p → depth p < depth t
  priority_monotone : ∀ t p, parent t = some p → priority t ≤ priority p

def Born {α : Type} (h : Hierarchy α) (selected : Region α) (t : α) : Prop :=
  match h.parent t with
  | none => True
  | some p => selected p

def Fits {α : Type} (h : Hierarchy α) (selected : Region α) (threshold : Nat) : Prop :=
  ∀ t, Born h selected t → ¬selected t → h.priority t ≤ threshold

def Required {α : Type} (h : Hierarchy α) (threshold : Nat) : Region α :=
  fun t => threshold < h.priority t

theorem required_necessary {α : Type} (h : Hierarchy α) (selected : Region α)
    (threshold : Nat) (fits : Fits h selected threshold) :
    Included (Required h threshold) selected := by
  have aux : ∀ d, ∀ t, h.depth t = d → threshold < h.priority t → selected t := by
    intro d
    induction d using Nat.strongInductionOn with
    | ind d ih =>
      intro t hd bad
      have born : Born h selected t := by
        unfold Born
        cases hp : h.parent t with
        | none => trivial
        | some p =>
          apply ih (h.depth p)
          · rw [← hd]
            exact h.parent_decreases t p hp
          · exact rfl
          · exact Nat.lt_of_lt_of_le bad (h.priority_monotone t p hp)
      by_cases present : selected t
      · exact present
      · exact False.elim ((Nat.not_le_of_gt bad) (fits t born present))
  intro t ht
  exact aux (h.depth t) t rfl ht

theorem required_sufficient {α : Type} (h : Hierarchy α) (selected : Region α)
    (threshold : Nat) (contains : Included (Required h threshold) selected) :
    Fits h selected threshold := by
  intro t _ missing
  exact Nat.le_of_not_gt (fun bad => missing (contains t bad))

theorem fits_iff_required {α : Type} (h : Hierarchy α) (selected : Region α)
    (threshold : Nat) : Fits h selected threshold ↔ Included (Required h threshold) selected :=
  ⟨required_necessary h selected threshold, required_sufficient h selected threshold⟩

def Target {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial : Region α) (threshold : Nat) : Region α :=
  Closure edge (Join initial (Required h threshold))

def Admissible {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial allowed : Region α) (threshold : Nat) (selected : Region α) : Prop :=
  Included initial selected ∧ Closed edge selected ∧ Included selected allowed ∧
    Fits h selected threshold

theorem target_least {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial allowed : Region α) (threshold : Nat) {selected : Region α}
    (ok : Admissible h edge initial allowed threshold selected) :
    Included (Target h edge initial threshold) selected := by
  refine @closure_least α edge (Join initial (Required h threshold)) selected ?_ ok.2.1
  intro t ht
  rcases ht with hi | hr
  · exact ok.1 t hi
  · exact required_necessary h selected threshold ok.2.2.2 t hr

theorem target_admissible {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial allowed : Region α) (threshold : Nat)
    (terminalCheck : Included (Target h edge initial threshold) allowed) :
    Admissible h edge initial allowed threshold (Target h edge initial threshold) := by
  refine ⟨?_, closure_closed edge _, terminalCheck, ?_⟩
  · intro t ht
    exact closure_extensive edge _ t (Or.inl ht)
  · apply required_sufficient
    intro t ht
    exact closure_extensive edge _ t (Or.inr ht)

theorem target_monotone {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial : Region α) {lower upper : Nat} (order : lower ≤ upper) :
    Included (Target h edge initial upper) (Target h edge initial lower) := by
  apply closure_monotone edge
  intro t ht
  rcases ht with hi | hr
  · exact Or.inl hi
  · exact Or.inr (Nat.lt_of_le_of_lt order hr)

theorem forbidden_target_impossible {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial allowed : Region α) (threshold : Nat)
    (blocked : ∃ t, Target h edge initial threshold t ∧ ¬allowed t) :
    ¬∃ selected, Admissible h edge initial allowed threshold selected := by
  rintro ⟨selected, ok⟩
  obtain ⟨t, ht, forbidden⟩ := blocked
  exact forbidden (ok.2.2.1 t (target_least h edge initial allowed threshold ok t ht))

-- 有限枚举中每个事件应只出现一次；代价单调定理本身对任意枚举均成立
noncomputable def WeightSum {α : Type} (items : List α) (weight : α → Nat)
    (selected : Region α) : Nat := by
  exact items.foldr (fun x acc => (if selected x then weight x else 0) + acc) 0

theorem weight_sum_monotone {α : Type} (items : List α) (weight : α → Nat)
    {a b : Region α} (hab : Included a b) : WeightSum items weight a ≤ WeightSum items weight b := by
  induction items with
  | nil => exact Nat.le_refl 0
  | cons x xs ih =>
    change (if a x then weight x else 0) + WeightSum xs weight a ≤
      (if b x then weight x else 0) + WeightSum xs weight b
    apply Nat.add_le_add _ ih
    by_cases ha : a x
    · rw [if_pos ha, if_pos (hab x ha)]
      exact Nat.le_refl _
    · rw [if_neg ha]
      exact Nat.zero_le _

theorem weight_sum_union_intersection {α : Type} (items : List α) (weight : α → Nat)
    (a b : Region α) :
    WeightSum items weight (Join a b) + WeightSum items weight (Meet a b) =
      WeightSum items weight a + WeightSum items weight b := by
  induction items with
  | nil => rfl
  | cons x xs ih =>
    by_cases ha : a x <;> by_cases hb : b x <;>
      simp only [WeightSum, List.foldr_cons, Join, Meet, ha, hb,
        true_or, or_true, false_or, or_false, true_and, and_true,
        false_and, and_false, if_true, if_false] at * <;> omega

-- 次模的是闭包的非负加权成本；这里没有把最大几何误差改写成可加收益
theorem closure_cost_submodular {α : Type} (items : List α) (weight : α → Nat)
    (edge : α → α → Prop) (a b : Region α) :
    WeightSum items weight (Closure edge (Join a b)) +
      WeightSum items weight (Closure edge (Meet a b)) ≤
    WeightSum items weight (Closure edge a) + WeightSum items weight (Closure edge b) := by
  rw [closure_union]
  have mono := weight_sum_monotone items weight (closure_intersection_subset edge a b)
  have identity := weight_sum_union_intersection items weight (Closure edge a) (Closure edge b)
  omega

theorem hard_budget_iff {α : Type} (h : Hierarchy α) (edge : α → α → Prop)
    (initial allowed : Region α) (threshold baseCount budget : Nat)
    (items : List α) (weight : α → Nat) :
    (∃ selected, Admissible h edge initial allowed threshold selected ∧
      baseCount + WeightSum items weight selected ≤ budget) ↔
    (Included (Target h edge initial threshold) allowed ∧
      baseCount + WeightSum items weight (Target h edge initial threshold) ≤ budget) := by
  constructor
  · rintro ⟨selected, ok, cap⟩
    have smaller := target_least h edge initial allowed threshold ok
    constructor
    · intro t ht
      exact ok.2.2.1 t (smaller t ht)
    · exact Nat.le_trans (Nat.add_le_add_left (weight_sum_monotone items weight smaller) _) cap
  · rintro ⟨terminalCheck, cap⟩
    exact ⟨Target h edge initial threshold,
      target_admissible h edge initial allowed threshold terminalCheck, cap⟩

#print axioms required_necessary
#print axioms target_least
#print axioms forbidden_target_impossible
#print axioms hard_budget_iff
#print axioms closure_cost_submodular

end RoamThreshold
