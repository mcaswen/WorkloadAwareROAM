import StateModel

namespace RoamMaterialization

attribute [local instance] Classical.propDecidable

theorem event_agrees_outside_leaf_support {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (t : α) (outside : ¬LeafSupport h i j t) : i t ↔ j t := by
  apply Classical.byContradiction
  intro different
  exact outside (Or.inl different)

theorem leaf_agrees_outside_support {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (t : α) (outside : ¬LeafSupport h i j t) :
    Leaf h i t ↔ Leaf h j t := by
  have self := event_agrees_outside_leaf_support h i j t outside
  have born : Born h i t ↔ Born h j t := by
    cases hp : h.parent t with
    | none => simp [Born, hp]
    | some p =>
      have parent : i p ↔ j p := by
        apply Classical.byContradiction
        intro different
        exact outside (Or.inr ⟨p, hp, different⟩)
      simpa [Born, hp] using parent
  exact and_congr born (not_congr self)

theorem leaf_change_supported {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (t : α) (different : Changed (Leaf h i) (Leaf h j) t) :
    LeafSupport h i j t := by
  apply Classical.byContradiction
  intro outside
  exact different (leaf_agrees_outside_support h i j t outside)

theorem merge_reads_agree_outside_support {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (d : δ) (outside : ¬MergeSupport h i j d) :
    ∀ t, h.diamond t = d → (i t ↔ j t) ∧
      ∀ c, h.parent c = some t → (i c ↔ j c) := by
  intro t member
  constructor
  · apply Classical.byContradiction
    intro different
    exact outside ⟨t, different, Or.inl member⟩
  · intro c child
    apply Classical.byContradiction
    intro different
    exact outside ⟨c, different, Or.inr ⟨t, child, member⟩⟩

theorem merge_ready_agrees_outside_support {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (d : δ) (outside : ¬MergeSupport h i j d) :
    MergeReady h i d ↔ MergeReady h j d := by
  have reads := merge_reads_agree_outside_support h i j d outside
  constructor
  · intro ready
    refine ⟨ready.1, ?_⟩
    intro t member
    refine ⟨(reads t member).1.mp (ready.2 t member).1, ?_⟩
    intro c child present
    exact (ready.2 t member).2 c child ((reads t member).2 c child |>.mpr present)
  · intro ready
    refine ⟨ready.1, ?_⟩
    intro t member
    refine ⟨(reads t member).1.mpr (ready.2 t member).1, ?_⟩
    intro c child present
    exact (ready.2 t member).2 c child ((reads t member).2 c child |>.mp present)

theorem merge_change_supported {α δ : Type} (h : Hierarchy α δ)
    (i j : Region α) (d : δ)
    (different : Changed (MergeReady h i) (MergeReady h j) d) :
    MergeSupport h i j d := by
  apply Classical.byContradiction
  intro outside
  exact different (merge_ready_agrees_outside_support h i j d outside)

-- 直接孩子条件保证：合并后不会保留已经失去父事件的细分事件。
theorem ready_merge_preserves_closed {α δ : Type} (h : Hierarchy α δ)
    (i : Region α) (d : δ) (closed : Closed h i) (ready : MergeReady h i d) :
    Closed h (RemoveDiamond h i d) := by
  constructor
  · intro t p present parent
    refine ⟨closed.1 t p present.1 parent, ?_⟩
    intro removedParent
    exact (ready.2 p removedParent).2 t parent present.1
  · intro t u present sameGroup
    refine ⟨closed.2 t u present.1 sameGroup, ?_⟩
    intro removedPartner
    exact present.2 (sameGroup.symm.trans removedPartner)

theorem target_preserves_history_and_blocked {α : Type} (m : Marks α)
    (i j : Region α) :
    (TargetMarks m i j).history = m.history ∧
    (TargetMarks m i j).blocked = m.blocked := by
  exact ⟨rfl, rfl⟩

theorem split_entry_agrees_outside_support {α δ : Type} (h : Hierarchy α δ)
    (score : α → Nat) (i j : Region α) (m : Marks α) (t : α)
    (outside : ¬LeafSupport h i j t) :
    SplitEntry h score i m t = SplitEntry h score j (TargetMarks m i j) t := by
  have self := event_agrees_outside_leaf_support h i j t outside
  have leaf := propext (leaf_agrees_outside_support h i j t outside)
  have noRemoval : ¬(i t ∧ ¬j t) := fun pair => pair.2 (self.mp pair.1)
  simp only [SplitEntry, TargetMarks, noRemoval, or_false, leaf]

theorem merge_entry_agrees_outside_support {α δ : Type} (h : Hierarchy α δ)
    (score : δ → Nat) (i j : Region α) (m : Marks α) (d : δ)
    (outside : ¬MergeSupport h i j d) :
    MergeEntry h score i m d = MergeEntry h score j (TargetMarks m i j) d := by
  have ready := propext (merge_ready_agrees_outside_support h i j d outside)
  have marks :
      (∀ t, h.diamond t = d → ¬m.split t) ↔
      (∀ t, h.diamond t = d → ¬(TargetMarks m i j).split t) := by
    have reads := merge_reads_agree_outside_support h i j d outside
    constructor
    · intro clean t member split
      cases split with
      | inl old => exact clean t member old
      | inr added => exact added.2 ((reads t member).1.mpr added.1)
    · intro clean t member old
      exact clean t member (Or.inl old)
  simp only [MergeEntry, ready, propext marks]

-- 支持集内重算，外部复用；完整条目等式包含资格、抑制位和分数。
theorem repair_split_queue_exact {α δ : Type} (h : Hierarchy α δ)
    (score : α → Nat) (i j : Region α) (m : Marks α) :
    Repair (LeafSupport h i j) (SplitEntry h score i m)
      (SplitEntry h score j (TargetMarks m i j)) =
      SplitEntry h score j (TargetMarks m i j) := by
  funext t
  by_cases affected : LeafSupport h i j t
  · simp [Repair, affected]
  · simp only [Repair, if_neg affected]
    exact split_entry_agrees_outside_support h score i j m t affected

theorem repair_merge_queue_exact {α δ : Type} (h : Hierarchy α δ)
    (score : δ → Nat) (i j : Region α) (m : Marks α) :
    Repair (MergeSupport h i j) (MergeEntry h score i m)
      (MergeEntry h score j (TargetMarks m i j)) =
      MergeEntry h score j (TargetMarks m i j) := by
  funext d
  by_cases affected : MergeSupport h i j d
  · simp [Repair, affected]
  · simp only [Repair, if_neg affected]
    exact merge_entry_agrees_outside_support h score i j m d affected

end RoamMaterialization
