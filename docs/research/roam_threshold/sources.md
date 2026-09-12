# 来源、已有工作与源码对应

> 核对日期：2026-09-12。优先使用原始论文、作者机构页面和工具官方文档。
> 本轮是定向核对，不是完整系统综述；未检索到完全相同表述不构成新颖性证明。

## 1. 原始工作

| 来源 | 支持的内容 | 对本轮的约束 |
| --- | --- | --- |
| Duchaineau et al., **ROAMing Terrain: Real-time Optimally Adapting Meshes**, 1997，[DOI](https://doi.org/10.1109/VISUAL.1997.663860)，[项目内原文转录](../../source_analysis/roaming_terrain_paper.md) | §4 配对与 forced split；§5 单调 priority 下 greedy 的最优性；§6 几何上界 | 不能把阈值表达或最大 priority 最优性包装为本轮首创；原文最优性限定于其网格空间 |
| Pajarola, **Large Scale Terrain Visualization Using the Restricted Quadtree Triangulation**, 1998，[作者机构论文入口](https://www.ifi.uzh.ch/en/vmml/publications/vis-98.html)，[ETH 技术报告版本](https://www.research-collection.ethz.ch/bitstreams/f61fb9f9-81d1-4ff1-af01-c6faced19b26/download) | 规则层次、顶点依赖、阈值选择与一致三角剖分；报告与会议版本须区分 | “选择超阈值对象，然后补依赖”已有直接先例；不能声称发现了此前未知的依赖 DAG |
| Lindstrom and Pascucci, **Terrain Simplification Simplified: A General Framework for View-Dependent Out-of-Core Visualization**, 2002，[LLNL 原文](https://computing.llnl.gov/sites/default/files/Terrain%20Simplification%20Simplified.pdf) | 层次细化、嵌套误差条件、将依赖约束纳入误差处理、无持久状态的目标生成 | error saturation、依赖前置处理及从头生成目标网格不能单独构成本轮新颖性 |

Pajarola 作者站 PDF 在本轮部分请求中超时；保留作者索引及 ETH 机构版本，不把无法抓取当作论文不存在。ROAM 技术判断同时核对了项目保存的原文 §4–6。

补充阅读：Weiss and De Floriani, **Simplex and Diamond Hierarchies: Models and Applications**, 2010，[作者保存的综述](https://kennyweiss.com/papers/Weiss10.eg_star.pdf)。它用于定位 diamond 与层次依赖的既有术语和更早来源，不作为本轮新定理的原始出处。

## 2. 形式化工具依据

- [Lean 官方 Axioms and Computation](https://lean-lang.org/theorem_proving_in_lean4/Axioms-and-Computation/)：内核检查、标准逻辑基础、`#print axioms` 与可计算性边界。
- [Lean 4.8.0 官方发行](https://github.com/leanprover/lean4/releases/tag/v4.8.0)：本轮冻结的独立证明工具链。使用随发行附带的库，没有引入 Mathlib 或项目 C++ 依赖。

形式化中的 `Classical.choice`、`propext`、`Quot.sound` 是披露的标准逻辑基础。没有引入“ROAM 固定依赖成立”“误差必选等价成立”等自定义公理；后者在抽象层次模型内有实际证明。几何模型与 C++ 对应没有编码，不能因没有自定义公理而忽略这一缺口。

## 3. 当前实现依据

以下以提交 `1601717` 的源码和既有[Classic 对照](../../source_analysis/classic_roam_paper_comparison.md)为准；本轮不修改这些文件。

| 位置 | 已观察事实 | 不能据此推出的结论 |
| --- | --- | --- |
| [RoamGeometry.h](../../../src/algorithms/RoamGeometry.h)，`SplitTriangleDomain` | 固定参数域二分能力 | 尚未证明完整合法网格与 Lean 事件集双射 |
| [DataOrientedRoamTopology.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp)，`SplitNode` | 深度限制、预算取得、递归 forced split、重新读取邻居 | 一次运行 trace 不是潜在层次全部依赖；预算中断后的部分执行不自动等于完整数学闭包 |
| [RoamScreenError.h](../../../src/algorithms/RoamScreenError.h)，`ComputeScreenErrorScore` | 视锥处理后使用几何项与屏幕边长密度项的最大值 | 混合 score 不能等同独立几何误差；其完整父子单调性未证明 |
| [RoamNestedWedgie.h](../../../src/algorithms/RoamNestedWedgie.h)，`BuildNestedWedgieSubtree` | 终端厚度置零；上层累积子厚度及中点位移 | 终端零厚度不能自动成为相对 continuous bilinear source 的零误差证明 |

混合 score 的几何项若确实上界某个指定 reference 的误差，取 `max(geometry,density)` 仍不小于该几何项；不能仅因加入 density 就断言“绝不可能是上界”。当前缺口是 reference 一致性、终端残差、完整投影规则和单调性尚未形成证明链。

## 4. 新颖性判断

本轮可以诚实称为**项目内推导与机械检查成果**：

- 把闭包并集性质、叶条件必要性、禁止终端和硬预算判定接成显式证明链。
- 给出共享前置的激活阈值表示，并将参考误差与调度 priority 的条件分别列出。
- 通过形式化反例排除若干错误推广，并把选择、几何计算、状态更新的成本分开计入。

这些不自动成为论文算法贡献。固定 DAG、闭包、阈值、最大值传播和排序计数均有成熟基础；目前没有足够证据宣称组合后的方法首次出现。若后续存在贡献，更可能落在**具有明确条件的增量维护算法、整体并行成本或相对现有执行方法的实证改进**，需要另行验证。

本轮没有将一般 precedence-constrained knapsack 的困难性当作 ROAM 的复杂性证明，也没有把 maximum-closure/min-cut 当作已经解决硬预算问题。相应候选路线没有进入已证明算法。
