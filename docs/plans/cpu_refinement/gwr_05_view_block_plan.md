# GWR-05：不可见参考块的精确排除

> 2026-09-14。GWR-04 已闭环。仅考察一个保守块查询，不新建误差层次算法。前测复用 GWR-04 after，保存其二进制。

## 条件审计与准入证明

现有 Project 先计算 raw reference 的 clip；只有 reference 可见才检查被测近面并计算误差。若能证明某块所有 reference clip 都有限且不可见，则原规范逐样本结果必为 Visible=false、ErrorSquared=0，且不会执行被测近面检查。可直接填这些规范值。

新 TransactionalViewEvidence.h/.cpp 保存每连续 256 个 sample 的 binary64 参数 u/v 与已计算 ReferenceHeight 的 min/max，初建 O(q)，空间 O(q/256)。它们不依赖 topology、owner 或 surviving height；SetView 已禁止改变 TerrainSize/HeightScale/source，故不需要拓扑失效维护。

查询按原 Clip 的括号和运算构造向外舍入区间。每次中间操作遇到非有限输入/结果，整个范围退化为 unknown，不允许通过乘零等操作隐藏 overflow/NaN。四个 clip 分量的界都有限才可排除：w.upper<=0，或某个 x/y/z 满足 component.upper < -w.upper 或 component.lower > w.upper。严格不等式避免把 frustum 边界误判成不可见。

逐操作包含关系归纳给出：规范每个 binary64 clip 分量属于对应范围；上述判据保证 reference 可见性谓词为 false。只设置原本确定的零值；其他块全部调用原 Project。不能判定不是拒绝，不改变 NaN/overflow/near-plane 的报错责任。全 Q oracle 和有限随机/边界校验用于机械检查实现，不能替代一般包含关系论证。

因此无需近似 priority、top-r cutoff 或 stale cache：所有 SampleProjection 仍被完整填写，所有 face score、D_raw、donor max、witness 和后续查询照旧。此阶段只删除可证明不可见块的逐样本投影计算，**仍有 Θ(q) 输出写入和闭面关联归约**；不宣称完整 view update 次线性，也不追加 branch-and-bound top-r 算法。

## 文件和控制边界

Create ViewEvidence：静态范围和不可见证明，无 Samples/State/Pipeline 反向依赖；Extend Samples：初建累积范围，PrepareView 以块为分工单位，排除块填零，其他逐样本 Project。Execution 仍同一同步 1/4 线程接口，phase 名保持 view_projection。ViewState 的备用/发布与异常原子性不变。

Extend Types/Execution/Probe：ViewBoundsSamples、ViewBoundsBytes、ViewBlockChecks、ViewSkippedSamples。新增字节是构造总量，非 RSS。原 SampleEvaluations 改为实际调用 Project 的数量；减少部分由跳过数补足，不能把跳过样本当无质量证据。

## 验证、成本和停止

先针对边界/全不可见/部分可见/极端有限矩阵/overflow 的有限块检验：每个被判排除的样本必须由独立 Clip 判断有限且不可见。相关持续测试继续以完整 Refresh 做所有样本 oracle。两场景 C4 与 Peking A 八轮与原二进制比较精确 mesh/决策；构造、维护、查询、跳过率、全帧都计费。

若可靠性失败则撤回；若减少投影不抵消范围查询开销，保留审计结果但撤回运行候选。不再换块大小、增层或改质量定义寻找正结果。若通过，只关闭这个有限机制，完整 dense-view 成本与生产竞争力仍须在 GWR-06 如实报告。

## 实施结果

候选正确性有限验证通过，但机制性能 No-Go，运行改动已撤回。见[结果](../../research/cpu_refinement/gwr_05_view_block_results.md)与[审查](../../reviews/cpu_refinement/gwr_05_view_block_review.md)。不追加第二个层次查询机制，进入 GWR-06 成本闭环。
