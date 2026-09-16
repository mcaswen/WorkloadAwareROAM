import QualityComposition

namespace AdaptiveTransactions
attribute [local instance] Classical.propDecidable

noncomputable def run {I K V} (family : I → Transaction K V) : List I → State K V → State K V
  | [], s => s
  | i :: rest, s => run family rest (apply (family i) s)

/- 相邻交换、前缀保持和传递生成有限排列；不是预先假设执行相等。 -/
inductive Permutation {I : Type} : List I → List I → Prop
  | refl (xs) : Permutation xs xs
  | cons (i) {xs ys} : Permutation xs ys → Permutation (i :: xs) (i :: ys)
  | swap (i j) (xs) : Permutation (i :: j :: xs) (j :: i :: xs)
  | trans {xs ys zs} : Permutation xs ys → Permutation ys zs → Permutation xs zs

theorem run_permutation {I K V} (family : I → Transaction K V)
    (independent : ∀ i j, i ≠ j → Independent (family i) (family j))
    {xs ys : List I} (p : Permutation xs ys) :
    ∀ s, run family xs s = run family ys s := by
  induction p with
  | refl xs => intro s; rfl
  | cons i _ ih => intro s; exact ih (apply (family i) s)
  | swap i j xs =>
    intro s
    by_cases h : i = j
    · subst j; rfl
    · simp only [run]
      rw [apply_commute (family j) (family i) s (independent j i (Ne.symm h))]
  | trans _ _ ihp ihq =>
    intro s
    exact (ihp s).trans (ihq s)

theorem read_stable_run {I K V} (family : I → Transaction K V) (t : Transaction K V)
    (xs : List I) (s : State K V)
    (h : ∀ i ∈ xs, Independent t (family i)) :
    project t.reads (run family xs s) = project t.reads s := by
  induction xs generalizing s with
  | nil => rfl
  | cons i rest ih =>
    have ht : ∀ j ∈ rest, Independent t (family j) := fun j hj => h j (by simp [hj])
    rw [run, ih (apply (family i) s) ht]
    exact read_stable t (family i) s (h i (by simp)).2.2

theorem enabled_after_prefix {I K V} (family : I → Transaction K V) (t : Transaction K V)
    (xs : List I) (s : State K V)
    (h : ∀ i ∈ xs, Independent t (family i)) :
    enabled t (run family xs s) ↔ enabled t s := by
  unfold enabled
  rw [read_stable_run family t xs s h]

def IndependentBatch {I K V} (family : I → Transaction K V) : List I → Prop
  | [] => True
  | i :: rest => (∀ j ∈ rest, Independent (family i) (family j)) ∧ IndependentBatch family rest

noncomputable def frozenNet {I K V} (keys : List K) (family : I → Transaction K V)
    (seed : State K V) (xs : List I) : Int :=
  total xs (fun i => delta keys (family i) seed)

theorem total_congr {K} (keys : List K) (f g : K → Int)
    (h : ∀ k ∈ keys, f k = g k) : total keys f = total keys g := by
  induction keys with
  | nil => rfl
  | cons k rest ih =>
    simp only [total]
    rw [h k (by simp), ih (fun j hj => h j (by simp [hj]))]

theorem frozen_net_stable {I K V} (keys : List K) (family : I → Transaction K V)
    (seed : State K V) (xs : List I) (t : Transaction K V)
    (h : ∀ i ∈ xs, Independent (family i) t) :
    frozenNet keys family (apply t seed) xs = frozenNet keys family seed xs := by
  apply total_congr
  intro i hi
  exact delta_stable keys (family i) t seed (h i hi)

theorem count_run_frozen {I K V} (keys : List K) (family : I → Transaction K V)
    (xs : List I) (h : IndependentBatch family xs) (seed : State K V) :
    faceCount keys (run family xs seed) = faceCount keys seed + frozenNet keys family seed xs := by
  induction xs generalizing seed with
  | nil => simp [run, frozenNet, total]
  | cons i rest ih =>
    have ht : ∀ j ∈ rest, Independent (family j) (family i) :=
      fun j hj => independent_symm (h.1 j hj)
    rw [run, ih h.2, frozen_net_stable keys family seed rest (family i) ht, count_delta]
    simp [frozenNet, total, Int.add_assoc]

theorem endpoint_budget {I K V} (keys : List K) (family : I → Transaction K V)
    (xs : List I) (h : IndependentBatch family xs) (seed : State K V) (budget : Int)
    (fits : faceCount keys seed + frozenNet keys family seed xs ≤ budget) :
    faceCount keys (run family xs seed) ≤ budget := by
  rw [count_run_frozen keys family xs h seed]
  exact fits

theorem derived_observation_equal {I K V A} (family : I → Transaction K V)
    (independent : ∀ i j, i ≠ j → Independent (family i) (family j))
    {xs ys : List I} (p : Permutation xs ys) (seed : State K V)
    (restore : State K V → A) :
    restore (run family xs seed) = restore (run family ys seed) := by
  rw [run_permutation family independent p seed]

theorem create_delta_one : delta [0, 1] (createAt 0) (fun _ => none) = 1 := by
  simp [delta, total, apply, createAt, project, mass]

theorem delete_delta_negative_one :
    delta [0, 1] (deleteAt 1) (fun k => if k = 1 then some () else none) = -1 := by
  simp [delta, total, apply, deleteAt, project, mass]

theorem final_fits_but_prefix_and_subset_do_not :
    (10 + 2 - 2 : Int) ≤ 10 ∧ ¬((10 + 2 : Int) ≤ 10) := by decide

/- 零净额并不要求空写集：一个面身份消失，另一个面身份出现。 -/
def replaceFace : Transaction Nat Unit where
  reads := [0, 1]
  writes := [0, 1]
  guard := fun s => s 0 = some () ∧ s 1 = none
  output := fun _ k => if k = 1 then some () else none

theorem replace_delta_zero :
    delta [0, 1] replaceFace (fun k => if k = 0 then some () else none) = 0 := by
  simp [delta, total, apply, replaceFace, mass]

theorem actual_count_nonnegative {K V} (keys : List K) (s : State K V) :
    0 ≤ faceCount keys s := by
  have hm : ∀ k, 0 ≤ mass (s k) := by
    intro k
    cases s k <;> simp [mass]
  induction keys with
  | nil => simp [faceCount, total]
  | cons k rest ih =>
    simp only [faceCount, total] at *
    have hk := hm k
    omega

theorem batch_quality {I K V Q A} (s : QualityContract.Scalar A) (w : Weighting s)
    (o : Observation K V Q A) (points : List Q) (weight target : Q → A)
    (family : I → Transaction K V) (xs : List I) (seed : State K V)
    (positive : ∀ q ∈ points, s.le s.zero (weight q))
    (independent : IndependentBatch family xs)
    (locality : ∀ i ∈ xs, ObservationLocal o points (family i))
    (certificates : ∀ i ∈ xs, Certified s o points target (family i) seed) :
    s.le (potential s w points weight target (loss o (run family xs seed)))
         (potential s w points weight target (loss o seed)) := by
  induction xs generalizing seed with
  | nil => exact s.refl _
  | cons i rest ih =>
    have hc : ∀ j ∈ rest, Certified s o points target (family j) (apply (family i) seed) := by
      intro j hj
      exact certificate_migrates s o points target (family j) (family i) seed
        (independent_symm (independent.1 j hj))
        (locality j (by simp [hj])) (certificates j (by simp [hj]))
    have ht := ih (apply (family i) seed) independent.2
      (fun j hj => locality j (by simp [hj])) hc
    have hi := weighted_nonincrease s w points weight target (loss o seed)
      (loss o (apply (family i) seed)) positive (certificates i (by simp))
    exact s.trans ht hi

/- 组合机械核心；局部几何和生产映射仍由实例提供，未伪装成Lean几何定理。 -/
theorem budget_and_quality {I K V Q A} (s : QualityContract.Scalar A) (w : Weighting s)
    (o : Observation K V Q A) (points : List Q) (weight target : Q → A)
    (keys : List K) (family : I → Transaction K V) (xs : List I) (seed : State K V)
    (budget : Int) (positive : ∀ q ∈ points, s.le s.zero (weight q))
    (independent : IndependentBatch family xs)
    (locality : ∀ i ∈ xs, ObservationLocal o points (family i))
    (certificates : ∀ i ∈ xs, Certified s o points target (family i) seed)
    (fits : faceCount keys seed + frozenNet keys family seed xs ≤ budget) :
    faceCount keys (run family xs seed) ≤ budget ∧
    s.le (potential s w points weight target (loss o (run family xs seed)))
         (potential s w points weight target (loss o seed)) :=
  ⟨endpoint_budget keys family xs independent seed budget fits,
   batch_quality s w o points weight target family xs seed positive independent locality certificates⟩

#print axioms run_permutation
#print axioms enabled_after_prefix
#print axioms count_run_frozen
#print axioms endpoint_budget
#print axioms derived_observation_equal
#print axioms create_delta_one
#print axioms delete_delta_negative_one
#print axioms final_fits_but_prefix_and_subset_do_not
#print axioms replace_delta_zero
#print axioms actual_count_nonnegative
#print axioms batch_quality
#print axioms budget_and_quality
end AdaptiveTransactions
