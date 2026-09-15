# CEI-01 共享GPU观测与比较契约事实

日期：2026-09-16。仅描述统一数据、模式、矩阵和配对层；通用报告显示由下一阶段补齐。

## 文件与数据流

`runner → result_adapters → analysis`仍为唯一运行事实入口。新增`gpu_observations.py`由`analysis.summarize`调用；历史`cbt_report.py`也委托它，不再维护独立GPU去重规则。GPU记录不改写原始CSV，也不新增运行时读回。

`gpu_observations.samples(rows,generation_column,columns)`从当前资源/输出代建立来源机会索引，按计算、绘制或分类各自的采样代归属。零代、未知代、重复观测、未观测来源机会分别计数；矛盾重复、未来代、非有限/负值拒绝。结果保存来源机会与实际到达机会。

`gpu_observations.summarize`对每个冷/暖/移动/恢复/静止组按来源机会筛选。GPU计算总和、17个计算阶段、GPU绘制、分类工作计数分别保存均值/有效数量；不将GPU阶段加入CPU未归属时间。阶段名称顺序对应`TerrainLodCbtGpuStage`，第17号绘制使用独立采样代。

## Analysis字段与不变量

- `executionDevice`：CPU/GPU；`cpuTimeMeaning`：完整CPU更新或主机命令录制。
- `budgetMeaning`：CPU硬上限或CBT的CPU比较标签。
- `frames.faces`：仅同代当前实际捕获N；CBT普通帧仍为空。
- `frames.utilization`：CPU `N/B`；CBT不适用。`capturedPoolUtilization`另表述捕获N/(容量+6)。
- `gpu.observations`：来源代和观测到达机会；`gpu.groups`：分组有效GPU计时、计数及缺测覆盖。
- `transactionFrames`：仅Transactional适用，其他算法为空，不能把CBT解释成零事务收敛。

`analysis.build`拒绝同一个运行路径重复作为独立进程。不同目录同名`timing/visual`允许载入，显示ID加来源限定和短哈希，`manifestId`保留原清单身份。原始manifest不修改。

同任务线程成本比需要双方非空真实mesh证据、CPU设备、相同程序/输入/后端、公开结果一致和不同线程数量。两个GPU空hash相等不能获得`speedupClaimAllowed`。

质量文件仍验证SHA。`quality.valid_quality`同时检查成功状态和缺覆盖/歧义/几何/投影异常；成功退出但异常计数非零会在analysis中保留`originalStatus`并标无效，不改原始评价文件。

## 模式与逐点比较

`runner.compare_modes`对CPU保留公开hash/工作量核对。CBT返回`input-and-generation-contracts-only`，检查同执行身份及机会/相机/投影；各运行经适配器核对捕获代/容量/故障，不宣称GPU跨进程完整网格相同。

`quality.pointwise_excess`是共享纯数组原语；CBI旧报告和通用`pointwise_pair`均使用它。`pointwise_pair(..., allow_different_budget=False)`保持旧严格默认；显式放开预算后，仍核对原始源、参考/采样语义、地形尺度、视口、矩阵、Q、数组长度/有限性/可见域和来源文件SHA。逐点值为实际`max(e_candidate-e_reference)`，可以为负。

CLI新增`run_experiment.py pair --candidate ... --reference ... --output ... --allow-different-budget`。没有有效双方质量时保留unpaired；实际N写入配对结果。数学含义是同域误差比较，不是同数量性能证明。

## Suite适用维度

`suite.case_variants`按算法展开，`expand`负责落盘、逐case解析及哈希。输出`eip-suite-v2`记录维度规则，不运行算法。Classic只用1线程且不重复前缀；DOD仅预算×线程；Transactional预算×线程×前缀；CBT面积×容量，D3D12、线程占位1。

GPU配置不乘CPU预算/线程/前缀维度；CPU配置清除CBT字段；非Transactional清除翻边和高度策略。总数上限128。统一CLI支持`--algorithms ... cbt --cbt-areas ... --cbt-capacities ...`。

## 验证、成本和边界

16项受影响Python检查，以及CBI-04实际5运行混合分析、2组质量共8帧（含2无效）、跨预算有效/无效配对和9个真实解析suite配置均完成。CPU既有分组和工作量与修改前完全相同。

同CPU代表的读取/哈希约543.42→509.00ms；分析内核约0.782→0.763ms。单进程快验仅筛查工程影响，约19µs内核差值不作为性能研究结论。证据位于`benchmark-output/experiment-infrastructure/cei-01/`。

未改C++或着色器；CBI-03 CPU回归和canyon原始歧义仍OPEN。通用报告四算法展示尚待CEI-02，不能在此阶段声称全面接入已经完成。
