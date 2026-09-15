# PQ-05：受控净零翻边接入代码事实

2026-09-15；范围仅为本次新增翻边及其直接调用链。对照[已确认规划](../../plans/cpu_refinement/pq_05_flip_recovery_integration_plan.md)、[有限结果](../../research/cpu_refinement/pq_05_flip_recovery_results.md)。不将本页扩展为整个事务算法的重新设计。

## 1. 文件与职责

以下核心路径前缀为`src/algorithms/greedy_transactional_lod/`，namespace为`ParallelRoam::Algorithms::GreedyTransactionalLod`。

| 文件/类型 | FACT：本次职责与所有权 |
|---|---|
| `TransactionalFlipRecovery.h/.cpp` | 无跨帧状态；从只读State/根构造有限2→2连接、认证第一个合格项、提交前核对类型结构 |
| `TransactionalCertification.h/.cpp` | 新`SetProgressTarget`复用原见证/精确向下量化；Fit原位置调用，不改拟合求解 |
| `TransactionalTypes.h` | Configuration开关、ExchangeKind与新增账本/批次计数；旧默认聚合仍为Refinement |
| `TransactionalReservation.cpp` | 原目录全形状失败时调用恢复；混合项保留全局前缀位置，额度只对细分编号 |
| `TransactionalCommit.cpp` | 根据ExchangeKind校验净0/净+2/净−2义务，之后共用原私有准备与发布 |
| `TransactionalState.cpp` | 直接核心初建拒绝不支持的政策组合，防止绕过平台验证 |
| `TransactionalPipeline.cpp` | SetView禁止开关变化；原Apply仍负责统一拓扑/样本/网格提交 |
| `TransactionalSeedBuilder.cpp` | 公共设置传入Configuration；公共输入初检拒绝非法组合 |
| `TransactionalExecution.cpp` | 新四个uint64计数参与线程账本汇总；时间仍加`task_wall_sum_`前缀 |
| `TransactionalRenderBridge.cpp` | 新计数写入公共Stats，不参与选择和renderer资源管理 |
| `src/algorithms/TransactionalLodSettings.h` | `EnableFlipRecovery=false`；整个Settings既有相等比较决定种子生命周期 |
| `src/algorithms/TransactionalLodStats.h` | FlipTriggered/Attempts/Certified/Conflicts/Executed单列，原Exchanges仍为预算交换 |
| `src/benchmark/TransactionalPlatformReplay.cpp` | 显式回放参数和视觉模式；复用已有GraphicsBackend捕获、实际mesh导出 |
| `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.cpp` | 观察R事务的旧点/新面/矩阵；R不读取虚构NewVertex，不进入Fit反事实 |
| `tests/TransactionalQualityProvenanceProbe.cpp` | 有限C政策入口、账本、种子身份、默认B上的冻结AB对应；特殊frame/root只存在于诊断核查 |
| `tests/TransactionalFlipRecoveryTests.cpp` | 净零类型、满预算、失败原子性、混合反序、样本/mesh oracle和公共生命周期 |
| `scripts/run_transactional_flip_recovery.py` | 固定采集、对照、质量归约、真实帧比较和翻回分析；不进入运行计时 |
| `cmake/TransactionalLod.cmake`、`tests/CMakeLists.txt` | 核心编译单元及有限测试链接；复用原严格浮点配置 |

FACT：`TransactionalSamples`、`TransactionalMesh`、`TransactionalStateInvariant`未改；继续消费统一PreparedTopology。没有从experiment层调用恢复枚举来完成生产修改。PQ-04独立几何检查保留，没有让oracle转而调用新生产构造器。

## 2. 配置与生命周期

FACT：`EnableFlipRecovery`默认false；true仅允许`PreserveSurvivingHeights=true && HeightGuard=false`，State构造与SeedBuilder验证都拒绝其他组合。

公共适配器既有`SeedKey`包含完整TransactionalLodSettings。开关变化导致清理旧实例、重新建立种子；同一持续Pipeline中SetView检测到该字段变化会抛异常。它不是一个可以不重置就变更的view参数。公共Reset、零尺寸暂停、稳定政策继续执行沿用旧路径，并由新夹具实测。

配置流：

```text
TerrainLodSettings.Transactional.EnableFlipRecovery
  → SeedBuilder.ValidateInput / ConfigurationFor
  → InitialMesh.Config → TransactionalState._config
  → Reservation.Plan 的目录兜底
  → Commit.Prepare 的类型前提
```

FACT：GUI没有新增单独控件，公共API可设置该字段；有限平台入口可附加`--flip-recovery`且要求immutable事务模式。默认启动策略不变。

## 3. 局部构造与认证

`TransactionalFlipRecovery::Construct(state,root,edge)`返回Proposal，不改state：

1. 规范化无向边，查全局edge map；需要恰好2面且其中一面为root。
2. 只复制两面四点。两个对面点不同；新对角线在全局边表中不存在。
3. 两条对角线对各自另一对端点的精确orientation乘积均小于0。非凸、共线、已有新边直接失败。
4. 创建另一个对角线的两面并规范正方向，执行原最小角Shape。所有Point直接来自旧State，没有自由高度。
5. 返回Kind=`'R'`、两个Support、两个Faces、四个Points、空Free；成功构造Reason为空，仍不是已认证提案。

`FirstCertified`按根三条稳定边排序，最多3次，首个通过即返回。每项先Construct，几何成功后才VisibleSupport→SetProgressTarget→Measure→Accepts。没有样本、投影未知、质量失败都分别记录；未知不当作数学不可行。构造失败也计FlipAttempts，但不进入样本质量链。

FACT：`SetProgressTarget`选取Proposal.Samples中浮点ErrorSquared最大的第一个见证，使用内部ExactError取得其精确旧误差平方；整数sqrt/除法给出`floor(error*1e6)-10000`。空样本、近面/投影或数值问题保留原原因。Fit仅提取了这个职责，仍按原先形状判断之后、读Free之前调用。

INFERENCE：即使浮点见证不是精确最大点，旧见证误差也不大于旧支持最大值，因此通过该更保守目标仍意味着当前支持最大采样误差至少减少原.01px尺度。该论证不包含未来视图、连续曲面最大值或逐点不退化。

## 4. 选择、预算与计数

FACT：Plan先取得原精确global prefix，每个root的E/F/H仍按原序Fit。只有全部实际尝试非空、目录耗尽、原因均为`shape_infeasible`，且开关打开，才调用FirstCertified。质量失败、无样本、预算或reservation失败不会触发。

各worker独占certified/Attempts/IntentResults的一个前缀区间。完成后按原下标收集，不按完成顺序排序。成功翻边的IntentResult为`flip_certified`，失败为`flip_exhausted`；原E/F/H成功仍为`certified`。

`Receivers`只计Kind不是R的净+2项，`AssignedCredits=min((B-N)/2,Receivers)`。reservation遍历同一个混合序列；修复只检测已有资源然后占用，不增加`refinementOrdinal`。每遇细分项才使该序号递增，故R不能改变某细分是否属于预命名额度。失败额度仍不回流。

`ExchangeKind`附加在Receiver/Donor/HasDonor之后：旧`{receiver,donor,true}`与`{receiver,{},false}`仍按Refinement解释；R显式构造ConnectivityRepair。只有类型化的净0项可通过新提交分支，不能用`HasDonor=false`冒充。

自然探针逐帧断言：

```text
Exchanges.size() == Executed + FreeExecuted + FlipExecuted
N_after == N_before + 2*FreeExecuted
AssignedCredits == min((Budget-N_before)/2, Receivers)
```

FACT：WorkLedger四个新字段为Triggered/Attempts/Certified/Conflicts；FlipExecuted属于CertifiedBatch。原Raw/Need/Feasible/Executed/FreeExecuted等口径保留。构造失败的Reasons当前带`flip_flip_...`前缀，这是记录命名，不代表又进入一次恢复；报告按原因含义归类。

## 5. 冲突与完整续接

| 前提/写入 | FACT：已有资源或处理者 |
|---|---|
| 两个旧支持面 | Footprint中的f读/写键 |
| 四个旧点坐标、高度 | h读键；R没有h写；在整个快照内UV不可变 |
| 旧边关联和外接口 | e读/写键 |
| 新对角线不存在 | 新面所有边含该e读/写键；另一个创建该边的事务冲突 |
| 共享四点incident、导出资格/索引 | 原Commit准备中统一合成，再由Samples局部重算；没有同时写容器 |
| 有限输出块 | 原Mesh.prepare按已分配唯一槽填写，共享Pending统一合并 |

FACT：`IsUnchangedGeometryFlip`在Prepare阶段枚举旧支持面的至多三条边重建结构描述，比较规范连接、Support与四点精确几何。无额外质量重求值；绑定Version和`Reason=certified`仍沿原内部批次信任边界，不是接收任意外部不可信证书的安全接口。

ConnectivityRepair要求开关已打开、HasDonor=false、两面到两面、四旧点不变、Free为空。普通Refinement保留净+2规则及可选donor净−2规则；两类共同接受shape、冲突、预算和Version检查。类型伪造、改高、重复面等在发布前拒绝。

原流程不分叉：

```text
Commit.Prepare → Samples.Prepare → Mesh.Prepare → 配额检查
  → Commit.Publish → Samples.Publish → Mesh.Publish
```

新面身份由规范连接排序决定；物理槽复用旧支持，但稳定面身份推进。没有新Free点，NextVertexId不变。Samples修复旧支持/新面上的闭贡献、owner、误差、优先级、donor索引，并维护外部受影响面。Mesh写真实新面法线、顶点块和活动索引；Pending不因净面数为0而清空。准备失败只可能改变容量，不改变已发布逻辑状态/代际/脏记录。

## 6. 并发与复杂度

FACT：恢复发生在原receiver_stage的连续chunk内，共享State/Samples只读；任务局部Proposal/WorkLedger无竞争。reservation、共享邻接合成、最终publish仍串行。没有新线程池或并发容器写入。

INFERENCE：r个前缀中a个全形状失败根，几何查询至多3a次，每项O(log|Edges|)+常数规模点/面构造；提交结构核查同样有界局部查询。VisibleSupport仍需访问支持中的样本，不能把两面翻边叫作完整O(1)。单项认证含样本定位、区间或有理回退，受s与输入位长影响。预留仍可扫描b个已批准footprint；续接按实际变化样本/面和原索引修复计费。

FACT：`task_wall_sum_flip_recovery`是任务墙钟相加，包含在receiver_stage时间内，不能与receiver阶段相加成为完整时间。没有测量aggregate CPU time或新增严格span模型。

## 7. 诊断与验证边界

观察器对Kind R：旧点高度全部记录为旧值，`newPoint=null`，追加两面新旧连接与视图矩阵。四点身份/旧新对角线用于识别反向翻回，不用每次发布都会变化的面ID。未选中的`flip_certified`明确记录flipConflict，不再重新跑原E/F/H错误解释。

有限入口新增`visual-immutable`并可附加`--flip-recovery`；同视觉进程保存六帧真实capture和五份实际float mesh，正常路径没有截图或质量全扫。实验脚本验证B默认文件、B/C种子、core/platform、C1/C8及normal/visual对应；所有性能窗口保持既有CPU-ready边界。

Tests中的部分结构/反序/Pending夹具明确使用人工`certified`标记来隔离提交责任；真实数值正确性由Square真实认证、冻结AB对应和自然C承担。不能把人工标记测试描述成质量定理。混合批次的满预算计费和反序在夹具中验证；混合优先序/细分序号通过Plan源码审查、自然高优先根占用及逐帧预算断言共同核查，不声称穷举所有选择序列。

UNCERTAIN：不同地形/视图下是否有更多有效翻边、是否存在跨视图往返、是否可恢复到DOD质量，本阶段没有证据。FACT：本轮唯一轨迹仅一次有效翻边，未扩大原语或默认政策。
