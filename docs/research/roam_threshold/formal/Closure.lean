import Lean

namespace RoamThreshold

abbrev Region (α : Type) := α → Prop
def Included {α : Type} (a b : Region α) : Prop := ∀ x, a x → b x
def Join {α : Type} (a b : Region α) : Region α := fun x => a x ∨ b x
def Meet {α : Type} (a b : Region α) : Region α := fun x => a x ∧ b x
def Closed {α : Type} (edge : α → α → Prop) (s : Region α) : Prop :=
  ∀ a b, s a → edge a b → s b

-- 边从请求指向它所需要的前置事件，零步路径包含事件自身
inductive Reach {α : Type} (edge : α → α → Prop) : α → α → Prop where
  | refl (a) : Reach edge a a
  | step {a b c} : edge a b → Reach edge b c → Reach edge a c

def Closure {α : Type} (edge : α → α → Prop) (s : Region α) : Region α :=
  fun v => ∃ r, s r ∧ Reach edge r v

theorem reach_trans {α : Type} {edge : α → α → Prop} {a b c : α}
    (h : Reach edge a b) (k : Reach edge b c) : Reach edge a c := by
  induction h with
  | refl => exact k
  | step e _ ih => exact Reach.step e (ih k)

theorem closed_reach {α : Type} {edge : α → α → Prop} {s : Region α}
    (hs : Closed edge s) {a b} (ha : s a) (h : Reach edge a b) : s b := by
  induction h with
  | refl => exact ha
  | step e _ ih => exact ih (hs _ _ ha e)

theorem closure_extensive {α : Type} (edge : α → α → Prop) (s : Region α) :
    Included s (Closure edge s) := by
  intro x hx
  exact ⟨x, hx, Reach.refl x⟩

theorem closure_monotone {α : Type} (edge : α → α → Prop) {a b : Region α}
    (h : Included a b) : Included (Closure edge a) (Closure edge b) := by
  intro x hx
  obtain ⟨r, hr, path⟩ := hx
  exact ⟨r, h r hr, path⟩

theorem closure_closed {α : Type} (edge : α → α → Prop) (s : Region α) :
    Closed edge (Closure edge s) := by
  intro a b ha hab
  obtain ⟨r, hr, path⟩ := ha
  exact ⟨r, hr, reach_trans path (Reach.step hab (Reach.refl b))⟩

theorem closure_least {α : Type} (edge : α → α → Prop) {a b : Region α}
    (hab : Included a b) (hb : Closed edge b) : Included (Closure edge a) b := by
  intro x hx
  obtain ⟨r, hr, path⟩ := hx
  exact closed_reach hb (hab r hr) path

theorem closure_idempotent {α : Type} (edge : α → α → Prop) (s : Region α) :
    Closure edge (Closure edge s) = Closure edge s := by
  funext x
  apply propext
  exact ⟨closure_least edge (fun _ h => h) (closure_closed edge s) x,
    closure_extensive edge (Closure edge s) x⟩

theorem closure_union {α : Type} (edge : α → α → Prop) (a b : Region α) :
    Closure edge (Join a b) = Join (Closure edge a) (Closure edge b) := by
  funext x
  apply propext
  constructor
  · rintro ⟨r, (ha | hb), path⟩
    · exact Or.inl ⟨r, ha, path⟩
    · exact Or.inr ⟨r, hb, path⟩
  · intro h
    rcases h with ⟨r, hr, path⟩ | ⟨r, hr, path⟩
    · exact ⟨r, Or.inl hr, path⟩
    · exact ⟨r, Or.inr hr, path⟩

theorem closure_intersection_subset {α : Type} (edge : α → α → Prop) (a b : Region α) :
    Included (Closure edge (Meet a b)) (Meet (Closure edge a) (Closure edge b)) := by
  intro x hx
  exact ⟨closure_monotone edge (fun _ h => h.1) x hx,
    closure_monotone edge (fun _ h => h.2) x hx⟩

-- 已执行集合必须闭合，相对闭包的输入限定为尚未执行的事件
def RelativeClosure {α : Type} (edge : α → α → Prop)
    (initial s : Region α) : Region α := fun x => Closure edge s x ∧ ¬initial x

theorem relative_extensive {α : Type} (edge : α → α → Prop)
    (initial s : Region α) (fresh : ∀ x, s x → ¬initial x) :
    Included s (RelativeClosure edge initial s) := by
  intro x hx
  exact ⟨closure_extensive edge s x hx, fresh x hx⟩

theorem relative_monotone {α : Type} (edge : α → α → Prop)
    (initial : Region α) {a b : Region α} (hab : Included a b) :
    Included (RelativeClosure edge initial a) (RelativeClosure edge initial b) := by
  intro x hx
  exact ⟨closure_monotone edge hab x hx.1, hx.2⟩

theorem relative_union {α : Type} (edge : α → α → Prop)
    (initial a b : Region α) :
    RelativeClosure edge initial (Join a b) =
      Join (RelativeClosure edge initial a) (RelativeClosure edge initial b) := by
  funext x
  apply propext
  simp only [RelativeClosure, closure_union, Join]
  exact ⟨fun h => h.1.elim (fun ha => Or.inl ⟨ha, h.2⟩)
    (fun hb => Or.inr ⟨hb, h.2⟩),
    fun h => h.elim (fun ha => ⟨Or.inl ha.1, ha.2⟩)
      (fun hb => ⟨Or.inr hb.1, hb.2⟩)⟩

theorem relative_idempotent {α : Type} (edge : α → α → Prop)
    (initial s : Region α) :
    RelativeClosure edge initial (RelativeClosure edge initial s) =
      RelativeClosure edge initial s := by
  funext x
  apply propext
  constructor
  · rintro ⟨⟨r, hr, path⟩, hx⟩
    obtain ⟨q, hq, qr⟩ := hr.1
    exact ⟨⟨q, hq, reach_trans qr path⟩, hx⟩
  · intro hx
    exact ⟨⟨x, hx, Reach.refl x⟩, hx.2⟩

theorem relative_matches_initial_union {α : Type} (edge : α → α → Prop)
    (initial s : Region α) (hi : Closed edge initial) :
    RelativeClosure edge initial (Join initial s) = RelativeClosure edge initial s := by
  funext x
  apply propext
  constructor
  · rintro ⟨⟨r, (hr | hr), path⟩, hx⟩
    · exact False.elim (hx (closed_reach hi hr path))
    · exact ⟨⟨r, hr, path⟩, hx⟩
  · rintro ⟨⟨r, hr, path⟩, hx⟩
    exact ⟨⟨r, Or.inr hr, path⟩, hx⟩

#print axioms closure_union
#print axioms relative_matches_initial_union
#print axioms relative_idempotent

end RoamThreshold
