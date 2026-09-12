import Lean

namespace RoamThreshold.Counterexamples

abbrev Bits := Bool × Bool × Bool
def Contains (x y : Bits) : Prop :=
  (x.1 = true → y.1 = true) ∧ (x.2.1 = true → y.2.1 = true) ∧
    (x.2.2 = true → y.2.2 = true)
def Union (x y : Bits) : Bits := (x.1 || y.1, x.2.1 || y.2.1, x.2.2 || y.2.2)

-- 第三个事件由两个请求联合触发，这是固定单前件关系之外的闭包系统
def Horn (x : Bits) : Bits := (x.1, x.2.1, x.2.2 || (x.1 && x.2.1))

theorem horn_extensive : ∀ a b c : Bool, Contains (a, b, c) (Horn (a, b, c)) := by
  unfold Contains Horn
  decide
theorem horn_monotone : ∀ a b c d e f : Bool,
    Contains (a, b, c) (d, e, f) → Contains (Horn (a, b, c)) (Horn (d, e, f)) := by
  unfold Contains Horn
  decide
theorem horn_idempotent : ∀ a b c : Bool, Horn (Horn (a, b, c)) = Horn (a, b, c) := by decide
theorem horn_union_fails :
    Horn (Union (true, false, false) (false, true, false)) ≠
      Union (Horn (true, false, false)) (Horn (false, true, false)) := by decide

-- 最大误差受两个最坏区域共同约束，不能给每个请求分配固定的可加收益
def WorstError (a b : Bool) : Nat := max (if a then 0 else 10) (if b then 0 else 10)
def Gain (a b : Bool) : Nat := WorstError false false - WorstError a b

theorem maximum_error_gain_not_additive :
    Gain true false + Gain false true ≠ Gain true true := by decide

-- false 是根，true 是其未生成的子节点；当前没有执行任何细分
def RawPriority (t : Bool) : Nat := if t then 10 else 0
def RawBorn (selected : Bool → Bool) (t : Bool) : Bool := if t then selected false else true
def RawFits (selected : Bool → Bool) (threshold : Nat) : Prop :=
  ∀ t, RawBorn selected t = true → selected t = false → RawPriority t ≤ threshold

theorem nonmonotone_hidden_child :
    RawFits (fun _ => false) 5 ∧
      (∃ t : Bool, 5 < RawPriority t ∧ (fun _ : Bool => false) t = false) := by
  unfold RawFits RawBorn RawPriority
  decide

-- 两个抽象网格分别有真实误差/认证上界 1/10 和 0/0，后者需要更多三角形
def ActualError (refined : Bool) : Nat := if refined then 0 else 1
def Certificate (refined : Bool) : Nat := if refined then 0 else 10
def MeshCount (refined : Bool) : Nat := if refined then 4 else 2

theorem certificate_failure_not_actual_infeasibility :
    (∀ m, ActualError m ≤ Certificate m) ∧
    (∃ m, MeshCount m ≤ 2 ∧ ActualError m ≤ 2) ∧
    ¬(∃ m, MeshCount m ≤ 2 ∧ Certificate m ≤ 2) := by
  unfold ActualError Certificate MeshCount
  decide

-- 中间可行解的成本/收益为 2/1；另两解为 0/0 和 3/3
-- 对任意非负有理价格 m/n，都不能让中间解同时优于另外两解
theorem penalty_misses_budget_optimum : ∀ m n : Nat, 0 < n →
    ¬(2 * m ≤ n ∧ 3 * n + 2 * m ≤ n + 3 * m) := by
  intro m n hn h
  omega

#print axioms horn_union_fails
#print axioms horn_monotone
#print axioms maximum_error_gain_not_additive
#print axioms nonmonotone_hidden_child
#print axioms certificate_failure_not_actual_infeasibility
#print axioms penalty_misses_budget_optimum

end RoamThreshold.Counterexamples
