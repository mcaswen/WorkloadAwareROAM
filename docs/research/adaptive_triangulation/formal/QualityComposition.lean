import QualityContract
import StateTransactions

namespace AdaptiveTransactions
open QualityContract

/- 实数/有理数非负乘法具有此保序律；不把像素损失量化为Nat。
   此文件检查显式代数前提下的定理，不机械构造Real实例。 -/
structure Weighting {A : Type} (s : Scalar A) where
  mul : A → A → A
  nonnegativeMono : ∀ {w x y}, s.le s.zero w → s.le x y → s.le (mul w x) (mul w y)

def potential {A Q} (s : Scalar A) (w : Weighting s) (points : List Q)
    (weight target loss : Q → A) : A :=
  s.totalCost (points.map (fun q => w.mul (weight q) (s.excess (loss q) (target q))))

theorem weighted_nonincrease {A Q} (s : Scalar A) (w : Weighting s) (points : List Q)
    (weight target old next : Q → A)
    (positive : ∀ q ∈ points, s.le s.zero (weight q))
    (certificate : ∀ q ∈ points, s.le (next q) (s.cap (old q) (target q))) :
    s.le (potential s w points weight target next) (potential s w points weight target old) := by
  have h := s.sum_mono (points.map (fun q =>
      (w.mul (weight q) (s.excess (next q) (target q)),
       w.mul (weight q) (s.excess (old q) (target q))))) (by
    intro pair hp
    obtain ⟨q, hq, rfl⟩ := List.mem_map.mp hp
    exact w.nonnegativeMono (positive q hq)
      ((s.pointwise_iff_excess (old q) (next q) (target q)).mp (certificate q hq)))
  simpa [potential, List.map_map] using h

/- 观察量显式读取资源投影；Q与环境被固定在该值中。
   几何定位、遮挡等若会变化，必须进入reads或另建证明。 -/
structure Observation (K V Q A : Type) where
  reads : Q → List K
  evaluate : Q → State K V → A

noncomputable def loss {K V Q A} (o : Observation K V Q A) (s : State K V) (q : Q) : A :=
  o.evaluate q (project (o.reads q) s)

theorem observation_frame {K V Q A} (o : Observation K V Q A) (t : Transaction K V)
    (seed : State K V) (q : Q)
    (h : ∀ k, k ∈ t.writes → k ∉ o.reads q) : loss o (apply t seed) q = loss o seed q := by
  have hp : project (o.reads q) (apply t seed) = project (o.reads q) seed := by
    funext k
    by_cases hr : k ∈ o.reads q
    · have hw : k ∉ t.writes := fun hk => h k hk hr
      simp [project, apply_frame t seed k hw, hr]
    · simp [project, hr]
  unfold loss
  rw [hp]

def ObservationLocal {K V Q A} (o : Observation K V Q A) (points : List Q)
    (t : Transaction K V) : Prop :=
  ∀ q ∈ points,
    (∀ k, k ∈ t.writes → k ∉ o.reads q) ∨
    (∀ k, k ∈ o.reads q → k ∈ t.reads)

noncomputable def Certified {K V Q A} (s : Scalar A) (o : Observation K V Q A)
    (points : List Q) (target : Q → A) (t : Transaction K V) (seed : State K V) : Prop :=
  ∀ q ∈ points, s.le (loss o (apply t seed) q) (s.cap (loss o seed q) (target q))

/- 被t改变的观察点，其全部依赖必须纳入t的证书读域。
   不受t影响的点可被别的事务改变；t在那里仍是恒等。 -/
theorem certificate_migrates {K V Q A} (s : Scalar A) (o : Observation K V Q A)
    (points : List Q) (target : Q → A) (a b : Transaction K V) (seed : State K V)
    (independent : Independent a b) (locality : ObservationLocal o points a)
    (certificate : Certified s o points target a seed) :
    Certified s o points target a (apply b seed) := by
  intro q hq
  rcases locality q hq with untouched | covered
  · rw [observation_frame o a (apply b seed) q untouched]
    exact s.le_cap_left _ _
  · have hb : ∀ k, k ∈ b.writes → k ∉ o.reads q :=
      fun k hk hr => independent.2.2 k hk (covered k hr)
    have hn : loss o (apply a (apply b seed)) q = loss o (apply a seed) q := by
      rw [apply_commute a b seed independent]
      exact observation_frame o b (apply a seed) q hb
    rw [hn, observation_frame o b seed q hb]
    exact certificate q hq

#print axioms weighted_nonincrease
#print axioms observation_frame
#print axioms certificate_migrates
end AdaptiveTransactions
