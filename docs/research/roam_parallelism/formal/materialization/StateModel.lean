import Lean

namespace RoamMaterialization

attribute [local instance] Classical.propDecidable

abbrev Region (α : Type) := α → Prop

/- 父关系和菱形身份表示逻辑层次；几何实现及每组成员数另作纸面证明。
   本模型不包含物理槽位、内存分配和浮点计算。 -/
structure Hierarchy (α δ : Type) where
  parent : α → Option α
  diamond : α → δ
  allowed : Region α

def Changed {α : Type} (a b : Region α) (t : α) : Prop := ¬(a t ↔ b t)

def Born {α δ : Type} (h : Hierarchy α δ) (i : Region α) (t : α) : Prop :=
  match h.parent t with
  | none => True
  | some p => i p

def Leaf {α δ : Type} (h : Hierarchy α δ) (i : Region α) (t : α) : Prop :=
  Born h i t ∧ ¬i t

def LeafSupport {α δ : Type} (h : Hierarchy α δ) (i j : Region α) (t : α) : Prop :=
  Changed i j t ∨ ∃ p, h.parent t = some p ∧ Changed i j p

/- 合并组读取成员事件位和直接孩子事件位。
   空组不成为候选；最深层不能细分，由合法输入另外限制。 -/
def MergeReady {α δ : Type} (h : Hierarchy α δ) (i : Region α) (d : δ) : Prop :=
  (∃ t, h.diamond t = d) ∧
  ∀ t, h.diamond t = d → i t ∧ ∀ c, h.parent c = some t → ¬i c

def MergeSupport {α δ : Type} (h : Hierarchy α δ) (i j : Region α) (d : δ) : Prop :=
  ∃ t, Changed i j t ∧
    (h.diamond t = d ∨ ∃ p, h.parent t = some p ∧ h.diamond p = d)

def Closed {α δ : Type} (h : Hierarchy α δ) (i : Region α) : Prop :=
  (∀ t p, i t → h.parent t = some p → i p) ∧
  (∀ t u, i t → h.diamond u = h.diamond t → i u)

def RemoveDiamond {α δ : Type} (h : Hierarchy α δ) (i : Region α) (d : δ) : Region α :=
  fun t => i t ∧ h.diamond t ≠ d

/- 历史集合来自完整旧状态，不能只从旧事件集合猜测。
   目标事务保持轮次、迟滞与阻止重试状态。 -/
structure Marks (α : Type) where
  history : Region α
  blocked : Region α
  split : Region α
  merged : Region α

def TargetMarks {α : Type} (m : Marks α) (i j : Region α) : Marks α where
  history := m.history
  blocked := m.blocked
  split := fun t => m.split t ∨ (j t ∧ ¬i t)
  merged := fun t => m.merged t ∨ (i t ∧ ¬j t)

/- 外层 none 表示不在队列，内层 none 表示存在但被本轮规则抑制。
   两类队列采用各自的抑制排序规则，不把哨兵当成几何分数。 -/
noncomputable def SplitEntry {α δ : Type} (h : Hierarchy α δ)
    (score : α → Nat) (i : Region α) (m : Marks α) (t : α) : Option (Option Nat) :=
  if Leaf h i t then
    some (if h.allowed t ∧ ¬m.blocked t ∧ ¬m.merged t then some (score t) else none)
  else none

noncomputable def MergeEntry {α δ : Type} (h : Hierarchy α δ)
    (score : δ → Nat) (i : Region α) (m : Marks α) (d : δ) : Option (Option Nat) :=
  if MergeReady h i d then
    some (if ∀ t, h.diamond t = d → ¬m.split t then some (score d) else none)
  else none

noncomputable def Repair {α β : Type} (support : Region α) (old desired : α → β) : α → β :=
  fun x => if support x then desired x else old x

end RoamMaterialization
