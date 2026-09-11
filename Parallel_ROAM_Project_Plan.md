# Parallel ROAM Classic/DOD 基线规划

> 当前研究入口（2026-09-12）：[CPU ROAM 实验问题、贡献与后续工作](docs/plans/formal_experiment/cpu_roam_research_definition.md)。CPU-CBT 已[归档](obsolete/cpu_cbt/README.md)；下文保留基线与历史阶段约束，验证矩阵不是每次局部修改必须全跑的要求。

> 分支目标：从完整实验平台提取可复现的 Classic CPU ROAM 与 Data-Oriented CPU ROAM 双基线，为新的研究问题提供稳定起点。

## 当前状态

- Classic 和 DOD 使用相同高度图、视图、屏幕空间误差、三角形预算和验证口径；
- 两条算法均支持持久拓扑状态、split/merge、局部约束和增量 CPU mesh emit；
- OpenGL 与 D3D12 都通过同一 CPU mesh 输出契约消费两条算法；
- headless benchmark、runtime benchmark、CSV/Markdown 报告和图表脚本均以 Classic/DOD 为正式比较对象；
- 系统 CMake、vendored 依赖和固定 D3D12 runtime 保留，确保跨后端构建和实验复现能力。

## 基线冻结条件

该分支只有在以下条件全部满足后才能作为新仓库的初始根提交：

1. OpenGL RelWithDebInfo 构建通过；
2. D3D12 RelWithDebInfo 构建通过；
3. 全部 CTest 通过；
4. --benchmark --algorithm all --profile smoke 通过；
5. Classic/DOD 在相同输入下满足预算和拓扑约束；
6. 活跃源码、构建脚本、UI 和 CLI 不包含已移除研究路径；
7. README、依赖说明和实验文档能够从空构建目录复现。

## 公平比较原则

### 共享逻辑

以下能力属于算法比较合同，Classic 和 DOD 必须保持逻辑等价：

- nested wedgie/屏幕空间误差计算；
- split/merge 双阈值和候选优先级；
- 活动三角形预算；
- diamond split 与裂缝约束；
- 拓扑验证与统计定义；
- 增量 mesh 输出语义；
- benchmark 采样点和报告字段。

### 允许的 DOD 差异

DOD 可以直接利用以下数据导向特性：

- 稳定 node index；
- SoA 热字段布局；
- 连续 heap entry 和紧凑活动状态；
- 按字段批处理、SIMD 友好遍历和预取；
- 基于连续索引区间的 worker 分段；
- 由无指针稳定存储带来的并行预提交。

### 不允许的比较偏差

如果某项优化与 SoA、稳定索引或数据导向布局无关，并且 Classic 也能等价实现，则不能只在 DOD 中实现后直接归因于数据导向设计。应当：

- 在 Classic 中加入等价逻辑；或
- 将其作为独立通用优化，在两条路径中共同启用；或
- 给出明确证据说明 Classic 的对象/指针语义无法在不改变基线模型的前提下采用该实现。

## 已完成优化阶段

详细状态见 [Classic/DOD 优化计划](docs/parallel-roam/17-dod-classic-optimization-plan.md)。

- B 阶段：DOD serial/parallel 预算和提交职责分离、候选去重、直接标量访问；
- C1：heap entry 连续保存 score 与 node index，并分离活动叶节点输出顺序；
- C2：活动状态与旁路元数据压缩；
- D1：Classic/DOD 增量 mesh emit 与 dirty slot 上传；
- runtime 报告：Classic/DOD 逻辑阶段和渲染/上传指标对齐。

## 新研究问题接入规则

当前研究方向已由上述新定义页记录；具体实验与实现接入时仍必须：

1. 先写出可证伪研究假设和直接基线；
2. 新变体使用独立算法标识、配置和报告标签；
3. 不修改 Classic/DOD 冻结基线的默认行为；
4. 公共质量评估器独立于各算法内部 score；
5. 正确性门槛先于性能结论；
6. 所有实验能够由命令行从固定输入重放。

## 验证矩阵

| 验证 | OpenGL | D3D12 |
|---|---:|---:|
| RelWithDebInfo build | 必须 | 必须 |
| CTest | 必须 | 构建回归 |
| Classic smoke benchmark | 必须 | 可选交叉检查 |
| DOD smoke benchmark | 必须 | 可选交叉检查 |
| runtime benchmark 报告 | 必须 | 可选交叉检查 |
| GUI 算法切换 | 必须 | 必须 |

## 文档入口

- [当前研究定义](docs/plans/formal_experiment/cpu_roam_research_definition.md)：多阶段执行、CPU 并行优化、贡献边界、相关工作与待完成验证；
- [README](README.md)：构建、运行和仓库总览；
- [里程碑](docs/parallel-roam/04-milestones.md)：实现阶段；
- [实验与 benchmark](docs/parallel-roam/05-experiments-and-benchmarks.md)：实验口径；
- [开发规范](docs/standards/development_guidelines.md)：开发规范；
- [依赖与构建](docs/parallel-roam/10-dependency-setup.md)：可复现依赖；
- [问题修复记录](docs/parallel-roam/11-bug-fix-log.md)：问题修复；
- [Classic/DOD 优化计划](docs/parallel-roam/17-dod-classic-optimization-plan.md)：双基线优化计划；
- [Workload-Aware 自适应执行研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md)：研究问题、假设和论文结果结构；
- [Workload-Aware 具体问题定义](docs/parallel-roam/19-workload-aware-problem-definition.md)：pass、二维 workload matrix 和 feature 定义；
- [Workload-Aware 策略定义](docs/parallel-roam/20-workload-aware-strategy-definition.md)：策略接口、基线、oracle 和消融；
- [Workload-Aware 实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md)：两层实验、规模、指标和验证标准。
- [Workload-Aware 源码改造规划](docs/parallel-roam/22-workload-aware-source-refactoring-plan.md)：源码 pass 判定、策略开关、Classic 对照和分阶段改造。
