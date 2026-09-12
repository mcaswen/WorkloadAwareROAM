import Threshold

namespace RoamThreshold

attribute [local instance] Classical.propDecidable

-- 该定义用于证明有限集合上的最大值性质，不是建议在线枚举所有可达对
noncomputable def SupportMaximum {α : Type} (items : List α)
    (edge : α → α → Prop) (priority : α → Nat) (v : α) : Nat := by
  exact items.foldr
    (fun r acc => max (if Reach edge r v then priority r else 0) acc) 0

theorem support_maximum_spec {α : Type} (items : List α)
    (edge : α → α → Prop) (priority : α → Nat) (v : α) (threshold : Nat) :
    threshold < SupportMaximum items edge priority v ↔
      ∃ r, r ∈ items ∧ Reach edge r v ∧ threshold < priority r := by
  induction items with
  | nil => simp [SupportMaximum]
  | cons r rs ih =>
    change threshold < max (if Reach edge r v then priority r else 0)
      (SupportMaximum rs edge priority v) ↔ _
    have maxRule (a b : Nat) : threshold < max a b ↔ threshold < a ∨ threshold < b := by
      omega
    rw [maxRule, ih]
    by_cases path : Reach edge r v
    · simp only [if_pos path, List.mem_cons]
      constructor
      · intro h
        rcases h with hr | ⟨s, hs, ps, qs⟩
        · exact ⟨r, Or.inl rfl, path, hr⟩
        · exact ⟨s, Or.inr hs, ps, qs⟩
      · rintro ⟨s, (same | hs), ps, qs⟩
        · subst s
          exact Or.inl qs
        · exact Or.inr ⟨s, hs, ps, qs⟩
    · simp only [if_neg path, Nat.not_lt_zero, false_or, List.mem_cons]
      constructor
      · rintro ⟨s, hs, ps, qs⟩
        exact ⟨s, Or.inr hs, ps, qs⟩
      · rintro ⟨s, (same | hs), ps, qs⟩
        · subst s
          exact False.elim (path ps)
        · exact ⟨s, hs, ps, qs⟩

theorem support_equals_closure {α : Type} (items : List α)
    (complete : ∀ r, r ∈ items) (edge : α → α → Prop)
    (priority : α → Nat) (v : α) (threshold : Nat) :
    threshold < SupportMaximum items edge priority v ↔
      Closure edge (fun r => threshold < priority r) v := by
  rw [support_maximum_spec]
  constructor
  · rintro ⟨r, _, path, bad⟩
    exact ⟨r, bad, path⟩
  · rintro ⟨r, bad, path⟩
    exact ⟨r, complete r, path, bad⟩

theorem target_activation {α : Type} (h : Hierarchy α) (items : List α)
    (complete : ∀ r, r ∈ items) (edge : α → α → Prop)
    (initial : Region α) (closedInitial : Closed edge initial) (threshold : Nat) :
    Target h edge initial threshold =
      (fun v => initial v ∨ threshold < SupportMaximum items edge h.priority v) := by
  funext v
  apply propext
  unfold Target
  rw [closure_union]
  change (Closure edge initial v ∨ Closure edge (Required h threshold) v) ↔ _
  constructor
  · intro hv
    rcases hv with hi | hr
    · exact Or.inl (closure_least edge (fun _ hi => hi) closedInitial v hi)
    · exact Or.inr ((support_equals_closure items complete edge h.priority v threshold).mpr hr)
  · intro hv
    rcases hv with hi | hr
    · exact Or.inl (closure_extensive edge initial v hi)
    · exact Or.inr ((support_equals_closure items complete edge h.priority v threshold).mp hr)

#print axioms support_equals_closure
#print axioms target_activation

end RoamThreshold
