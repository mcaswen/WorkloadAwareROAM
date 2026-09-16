import Lean

namespace AdaptiveTransactions

attribute [local instance] Classical.propDecidable

abbrev State (Key Value : Type) := Key → Option Value

/- 读取函数只能得到投影，不能借使能或输出函数读取未登记状态。
   有限列表提供资源域；重复键不产生重复写，计数域另行要求唯一。 -/
noncomputable def project {K V : Type} (reads : List K) (s : State K V) : State K V :=
  fun k => if k ∈ reads then s k else none

structure Transaction (K V : Type) where
  reads : List K
  writes : List K
  guard : State K V → Prop
  output : State K V → State K V

noncomputable def enabled {K V} (t : Transaction K V) (s : State K V) : Prop :=
  t.guard (project t.reads s)

noncomputable def apply {K V} (t : Transaction K V) (s : State K V) : State K V :=
  fun k => if k ∈ t.writes then t.output (project t.reads s) k else s k

def Independent {K V} (a b : Transaction K V) : Prop :=
  (∀ k, k ∈ a.writes → k ∉ b.writes) ∧
  (∀ k, k ∈ a.writes → k ∉ b.reads) ∧
  (∀ k, k ∈ b.writes → k ∉ a.reads)

theorem independent_symm {K V} {a b : Transaction K V} (h : Independent a b) :
    Independent b a :=
  ⟨fun k hb ha => h.1 k ha hb, h.2.2, h.2.1⟩

theorem apply_frame {K V} (t : Transaction K V) (s : State K V) (k : K)
    (h : k ∉ t.writes) : apply t s k = s k := by
  simp [apply, h]

theorem read_stable {K V} (a b : Transaction K V) (s : State K V)
    (h : ∀ k, k ∈ b.writes → k ∉ a.reads) :
    project a.reads (apply b s) = project a.reads s := by
  funext k
  by_cases hk : k ∈ a.reads
  · have hn : k ∉ b.writes := fun hw => h k hw hk
    simp [project, apply, hk, hn]
  · simp [project, hk]

theorem enabled_stable {K V} (a b : Transaction K V) (s : State K V)
    (h : Independent a b) : enabled a (apply b s) ↔ enabled a s := by
  unfold enabled
  rw [read_stable a b s h.2.2]

theorem apply_commute {K V} (a b : Transaction K V) (s : State K V)
    (h : Independent a b) : apply a (apply b s) = apply b (apply a s) := by
  funext k
  change (if k ∈ a.writes then a.output (project a.reads (apply b s)) k else apply b s k) =
    (if k ∈ b.writes then b.output (project b.reads (apply a s)) k else apply a s k)
  rw [read_stable a b s h.2.2, read_stable b a s h.2.1]
  by_cases ha : k ∈ a.writes
  · have hb : k ∉ b.writes := h.1 k ha
    simp [apply, ha, hb]
  · by_cases hb : k ∈ b.writes <;> simp [apply, ha, hb]

/- 面数由实际存在位求和，delta不是描述符中任意填写的整数。
   keys作为真实面域时须唯一且完整；本代数本身也允许带重计数列表。 -/
def mass {V} (value : Option V) : Int := if value.isSome then 1 else 0

def total {K} (keys : List K) (f : K → Int) : Int :=
  match keys with
  | [] => 0
  | k :: rest => f k + total rest f

def faceCount {K V} (keys : List K) (s : State K V) : Int :=
  total keys (fun k => mass (s k))

noncomputable def delta {K V} (keys : List K) (t : Transaction K V) (s : State K V) : Int :=
  total keys (fun k => mass (apply t s k) - mass (s k))

theorem total_sub {K} (keys : List K) (f g : K → Int) :
    total keys (fun k => f k - g k) = total keys f - total keys g := by
  induction keys with
  | nil => rfl
  | cons k rest ih =>
    simp only [total, ih]
    omega

theorem count_delta {K V} (keys : List K) (t : Transaction K V) (s : State K V) :
    faceCount keys (apply t s) = faceCount keys s + delta keys t s := by
  unfold delta faceCount
  rw [total_sub]
  omega

/- 写集合互斥保持旧存在位，读取互斥保持输出值，因此冻结delta稳定。
   创建/删除资格仍需在guard中读取生命周期，单凭计数式不授权操作。 -/
theorem delta_stable {K V} (keys : List K) (a b : Transaction K V) (s : State K V)
    (h : Independent a b) : delta keys a (apply b s) = delta keys a s := by
  unfold delta
  congr 1
  funext k
  by_cases ha : k ∈ a.writes
  · have hb : k ∉ b.writes := h.1 k ha
    have hp := read_stable a b s h.2.2
    simp only [apply, if_pos ha, if_neg hb]
    change mass (a.output (project a.reads (apply b s)) k) - mass (s k) =
      mass (a.output (project a.reads s) k) - mass (s k)
    rw [hp]
  · simp [apply_frame a _ k ha]

-- 有限状态的具体创建/删除，不依赖几何或抽象交换公理。
def createAt (key : Nat) : Transaction Nat Unit where
  reads := [key]
  writes := [key]
  guard := fun s => s key = none
  output := fun _ _ => some ()

def deleteAt (key : Nat) : Transaction Nat Unit where
  reads := [key]
  writes := [key]
  guard := fun s => s key = some ()
  output := fun _ _ => none

theorem concrete_create_delete_independent : Independent (createAt 0) (deleteAt 1) := by
  simp [Independent, createAt, deleteAt]

theorem absent_read_detects_conflict : ¬Independent (createAt 0) (deleteAt 0) := by
  simp [Independent, createAt, deleteAt]

#print axioms apply_frame
#print axioms read_stable
#print axioms enabled_stable
#print axioms apply_commute
#print axioms count_delta
#print axioms delta_stable
end AdaptiveTransactions
