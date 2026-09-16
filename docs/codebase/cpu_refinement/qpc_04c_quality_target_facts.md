# QPC-04C：误差目标诊断的代码事实

日期：2026-09-16。范围仅为本阶段的只读评分观察、薄离线归约及直接被审计公式；已有完整追溯/生产架构沿[QPC-01事实](qpc_01_quality_recovery_facts.md)、[QPC-04B事实](qpc_04b_boundary_integration_facts.md)。对应[小规划](../../plans/cpu_refinement/qpc_04c_quality_target_audit_plan.md)和[结果](../../research/cpu_refinement/qpc_04c_quality_target_results.md)。

## 1. 文件、职责与依赖

| 文件 | 本阶段实际职责 | 状态所有权 |
|---|---|---|
| `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h/.cpp` | Extend：静态`WritePrioritySnapshot`导出当前全部活动面评分分量 | 借用const State/Samples，局部文件流/前缀集合，返回即销毁 |
| `src/benchmark/experiment/TransactionalRecoveryTrace.cpp` | Extend：识别`--priority-audit`，在冻结见证组SourceFrame调用观察器 | 原Pipeline继续拥有状态；入口持有独立诊断计时文件流 |
| `scripts/analyze_transactional_quality_targets.py` | Create：冻结质量归约、采样位置审计、相同候选集合排序反事实、接受事件与兼容归约 | 单次进程持有数组/字典，输出summary；不运行或修改策略 |
| `docs/research/cpu_refinement/data/qpc_04c_quality_target/` | 规划前身份、归约结果、必要验证记录 | 小型入库证据；大文件保留忽略目录 |

FACT：实验C++类型仍是原`TransactionalQualityProvenance`，没有新生产类型或缓存。上层benchmark调用experiment，experiment只读algorithms；生产不依赖诊断、Python或独立评价器。154个生产算法文件哈希未改变。

```text
冻结resolved/witnesses
  → 原RecoveryTrace持续Pipeline
  → SetView完成
  → [priority-audit且frame==SourceFrame] WritePrioritySnapshot
  → 原Plan/Before/Apply/After及续接验证

保存的FER/QPC质量 + 原见证链 + 三份评分CSV
  → 身份检查/归约/同人口排序
  → summary.json → 阶段报告
```

## 2. 原生入口和生命周期

FACT：`RunTransactionalRecoveryTrace`沿用参数`--recovery-trace RESOLVED WITNESSES OUTPUT`，只允许最多一个额外选项`--boundary-audit`或`--priority-audit`。未知或同时给两个选项走原错误出口；普通入口不创建评分文件。没有新增GUI、公共算法设置或平台接线。

FACT：启用时入口创建`priority-audit.csv`，字段`frame,seconds`。逐机会原`SetView`返回后扫描已解析见证组；当前帧等于组`SourceFrame`时写`priority-<frame>.csv`，随后原批次决策继续。导出为同步主线程只读操作，没有新增任务派发；前提是SetView已完成且没有同时发布。局部引用不逃出方法。原输入见证组按SourceFrame唯一；本阶段没有提供任意帧范围或全轨迹逐帧全扫开关。

FACT：`WritePrioritySnapshot(path,const State&,const Samples&)`不接受`WorkLedger`，不会污染生产操作数量。它读取一次`Samples::Prefix(PrefixLimit)`，复制为局部`set<Slot>`，随后逐活动面：

1. 遍历既有`FaceSamples(slot)`闭面贡献，仅累积`Projection(sid).Visible`项的最大平方误差和数量。
2. 从三个顶点只读几何调用公开`Samples::Clip`；任何w非正时密度项为无穷，否则独立重算三个投影边的最大平方长度乘0.04。
3. 读取持久`PrioritySquared`、稳定面身份/槽位、前缀成员及配置/数量，17位精度写CSV。

输出字段为`root,slot,errorSquared,densitySquared,prioritySquared,visibleSamples,contributions,inPrefix,thresholdPx,prefixLimit,rawCount,faces`。误差/密度/评分前三项均为平方像素，阈值仍为像素；不能直接混比。CSV可表达无穷，离线统计将其单列，不将无穷项重新纳入生产资格。

FACT：文件流设置badbit/failbit异常；写出失败由现有追溯错误通道处理，可能保留未完成文件，不继续伪造报告成功。本次只有三个指定快照，没有中断或写出错误。诊断费用包含读状态、容器及文件输出，单独记录。

## 3. 离线方法与数据流

FACT：脚本常量绑定QPC-04C已批准的历史目录与三个快照，独立存在是因为本阶段的实验解释不属于通用质量公式/生产调度。主要复用`run_transactional_platform.identity`和`run_transactional_recovery_trace.checked_frame/sampling_coordinates`。

| 方法 | 实际行为 |
|---|---|
| `quality_panel(frozen)` | 按preplan顺序读取16份实际误差；checked_frame核对产物身份，另核对规划前index SHA；拒绝非sampled_only、缺失/歧义/跨近面/无效项；核对source/Q/view及逐点可见mask，再重算六档计数、Emax、比例和相对off逐点超额 |
| `sample_coordinates(width)` | 复用独立评价的坐标重建，转六分格整数检查分组/唯一性；比较已核对源码的运行时六组规则，记录共同理想位置和各自独有位置，再单列float UV差 |
| `snapshot(path,witnesses)` | 转换字段，重算max(error²,density²)；核对活动面完整性/稳定身份唯一、有限且大于阈值的资格数量；按原全序核对实际前缀，然后仅对同一人口按误差排序，记录根人口和原见证排名 |
| `acceptance_events()` | 从原04B事务JSONL取Canyon8、Peking200k13的B，按同帧exchange查donor；保存真实可见支持、旧最大、目标、新上界和见证几何，不重认证 |
| `compatibility(output)` | 原QPC-01全部CSV/JSONL与新增诊断运行逐字节比较；普通前后运行比较所有非`*Ms`字段，暖frame3–23只归约CPU时间 |
| `analyze(output)` | 拒绝覆盖summary；顺序调用上述步骤，保留旧见证链、源路径/SHA和进程成本；JSON拒绝NaN输出 |

FACT：独立误差数组以负值表示不可见样本；可见样本统一`>=0`，绝对操作点计数严格使用`>`。本阶段按旧数组重算得到与规划前相同的Emax和六档计数。`DmaxVsOffPx`的参考是同帧B关闭网格，不是DOD或连续真值；对DOD行也只表示DOD相对off。`aboveFraction`分母是可见样本数，不是像素面积。

FACT：评分排序键是`(-prioritySquared,root,slot)`；反事实键是`(-errorSquared,root,slot)`，稳定身份方向不变。`rank=0`表示未进入合资格集合，并不是实际第一名。生产前缀比较为成员集合，原全序用于重算排名；没有新事务结果。所有三个快照与历史见证排名一致。

## 4. 被审计公式与重要边界

FACT：`src/algorithms/greedy_transactional_lod/TransactionalSamples.cpp`中的采样重心余数为`(4,2)/(2,4)`；`TerrainMeshBuilder::Build`固定三角形及`MeshQualityEvaluator`顶点/边中点/重心发射使独立k=0评价重心为`(2,2)/(4,4)`。两者仍使用同一双线性源高度参考，不是两种primary surface。

FACT：Samples::Project以参考点clip可见性决定误差计算；参考可见时网格点w非正/跨近面报错，不跳过。Priority返回`max(maximum,0.04*longestEdgeSquared)`；任意顶点w非正返回无穷。索引只纳入有限且严格超过SplitPixels平方的值。全根诊断独立重算密度是审计需要，不是第二个生产评分实现。

FACT：`TransactionalCertification::SetProgressTarget`在提案可见支持中以浮点评分选见证，再对精确平方误差计算整数微像素向下取整并减10,000微像素。空可见支持、无效投影、数值范围和负目标显式拒绝。`Measure`取得提案支持最大误差区间；`Accepts`先用区间作确定比较，不确定时逐点exact比较。当前目标依赖提案支持，不等于根误差或绝对质量目标。

FACT：`TransactionalProposals`的donor证据在`Reservation`按receiver目标检查；原翻边恢复也调用SetProgressTarget/Measure/Accepts。04B的B取源高且禁止旧点改高，不经过Fit；本阶段抽取的两个实际B没有donor。

INFERENCE：相同运行时Q上局部最大值接受条件不约束独立Q_eval，亦不蕴含逐点单调或未来视图恢复。Canyon实际可见点退化提供了有限反例。仅采样位置差不能解释已在运行时见到的坏点为何未执行。

UNCERTAIN：未做相同算法对齐Q后的持续反事实，无法量化Q错位对全局质量或选项的贡献。未执行误差优先策略，无法证明其可行事务数量/质量/性能。

## 5. 复杂度与性能归属

定义N为活动面数，r为前缀数，C为全部活动面闭面贡献数量，V为当前顶点数。原生导出访问C个既有样本贡献和N个三角形的三个顶点，没有逐面重新定位全域Q。考虑现有顶点映射查询及前缀set，成本为`O(C+N log V+N log(r+1)+r log(r+1))`加CSV输出，额外内存O(r)及流缓冲；公共Prefix内部成本另由既有索引承担。不能简写为生产O(NQ)。

离线快照完整排序O(N log N)、空间O(N)，是诊断完整排名成本；不引入生产全排序。质量归约按固定16份数组O(q)，多份off基准会留到归约结束；采样集合`unique`有O(q log q)诊断成本。峰值/时间见研究结果，不能移出计费后称算法加速。

FACT：没有新增生产缓存失效、线程共享或持久状态。普通模式和原CSV/JSONL兼容对照已记录；原生CPU目标编译而非全部平台重建。PLANNED：误差优先策略、Q_target保证和绝对E*契约均尚未实现，不能被后续Agent当成当前生产能力。
