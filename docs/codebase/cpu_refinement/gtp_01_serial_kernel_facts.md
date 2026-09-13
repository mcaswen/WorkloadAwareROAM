# 事务化 CPU LOD 单批内核代码事实

2026-09-14；FACT，扫描范围为 `src/experiment/greedy_transactional_lod/` 全部源码、两个测试/探针、构建入口及独立核查脚本。对应[小规划](../../plans/cpu_refinement/gtp_01_serial_kernel_plan.md)与[结果](../../research/cpu_refinement/gtp_01_serial_kernel_results.md)。

## 1. 边界与文件索引

命名空间 `ParallelRoam::Experiment::GreedyTransactionalLod`。独立实验，无生产反向依赖、DOD 节点或外部目标。没有真实线程调度；所有方法当前串行。

| 文件/类型 | 状态、主要接口与调用关系 |
| --- | --- |
| `TransactionalTypes.h` | `Point/Triangle/Configuration/HeightSource/InitialMesh` 为输入值；`Proposal/Exchange/CertifiedBatch` 为局部决策值；`WorkLedger` 持有工作/时间/原因与配额 |
| `TransactionalState` | 唯一拥有 `_vertices/_faces/_activeFaces/_freeFaces/_freeVertices/_vertexIndex/_edges/_version`；初始化规范方向并建关联，公开只读访问；仅 Commit 是 friend |
| `TransactionalSamples` | `_source/_groups/_values/_faceSamples/_priority/_raw`；`Decode/Parameter` 保留整数比身份，`Refresh` 全量初建，`Weights/StrictlyInside` 分别判断闭面/严格面内；`VisibleSupport` 合并闭补丁样本 |
| `TransactionalPredicates` | `Orientation/Shape/Contains` 浮点过滤与有理回退；`Barycentric` 只返回拟合用近似权重 |
| `TransactionalCertification` | `Fit` 一/二维高度拟合、实际值认证；`Measure` 局部误差区间；`Accepts` 对接收阈值复用/精确比较；`ExactErrorSquared` 供解析验证 |
| `TransactionalProposals` | `Receivers` 固定 E/F/H 目录，H 包含中心完整邻面；`Ring` 按有向对边求环；`Donor` 固定 ID 耳切及误差测量，不运行 DP |
| `TransactionalReservation` | `Footprint/Conflict/Plan`；完整 P 前缀、先接收后共同池、几何 R/W、命名额度、不可回退选择；donor 证书仅同批缓存 |
| `TransactionalCommit` | `Apply` 核对代际/冲突/预算、合成局部面/点/边、预留容量、直接发布；不执行拟合或发现 |
| `TransactionalPipeline` | 拥有 State/Samples；`Update` 当前只允许一次，第二次明确报错；完整正常窗口不含独立验证或文件输出 |
| `TransactionalInput` | PropertyTree 读冻结 JSON，整数身份直接读 int64；`Write` 17 位小数输出真实几何，仅初始化/诊断使用 |
| `TransactionalValidation` | 全量独立派生邻接、面积/link/预算/状态核对；`Equivalent` 比较逻辑身份和几何，不比较物理槽顺序 |
| `GreedyTransactionalLodTests.cpp` | 33 样本/38 闭面贡献、2500/1600 误差、六价回收、边界舍入、冲突/预算/代际拒绝 |
| `GreedyTransactionalLodProbe.cpp` | diagnostic/timing 两模式，输入和输出拒绝覆盖；全量下一轮评分与反序只在诊断中运行 |
| `verify_transactional_cpp.py` | 独立 Python 发布网格与逐样本有理证书检查，解释重心舍入差异，不替 C++ 求目标 |

## 2. 数据与存活期

`InitialMesh → State + Samples`。State 自己保存几何，Source 在 Samples 内保存原始 uint16 值；二者无来源状态引用。初建会复制/排序初始化输入，但事务阶段不复制全网格。

顶点逻辑 ID 映射连续槽，面记录保存活动数组反向位置；局部移除面仅搬移活动数组尾部。空闲面/点槽可被后续直接应用复用；逻辑身份不重编号。当前负身份单调向下，临时新点按旧槽与目录序号形成互异身份，已批准面按连接排序分配。

每面闭样本数组与全 Q 唯一 owner 并存。当前 `Refresh` 重建全域关联和评价，只用于第一次更新及诊断；Samples 在提交后仍表示批次开始状态。`_updated` 防止调用者将这份旧样本关联误用于下一轮。

Proposal 持有完整局部点几何、旧支持槽、新面、自由高度 ID、可见样本和整数微像素目标。Donor 的 `ErrorLower/ErrorUpper` 缓存固定几何误差；每个接收阈值仍独立比较。Batch 包含版本、前缀/原因/池、批准事务及四层分母；不保存完整目标网格。

## 3. 正常控制流与数值

`Probe Load → Pipeline 初始化 → Update → Samples.Refresh → Reservation.Plan → Commit.Apply`。

Refresh 对格点包围框做样本位置测试；真实闭面归属使用整数比样本，不依赖 epsilon。唯一 owner 按稳定面 ID 决定，误差只计算一次，所有相关面共享贡献。参考可见域由冻结双精度矩阵规则确定；被测可见样本跨近面会报错。`P²=max(sampled error²,0.04*longest projected edge²)`；投影未知面不进入原始需求。

Plan 取完整原始全序的前 64 项，每项在目录内取第一个可认证接收方；失败不补取尾部。然后按最大 incident priority、稳定点 ID 取前 64 内部中心。预先分配 `(B−N)/2` 命名空额度；失败空额度本批闲置。后续需求枚举共同池计 `D_feasible`，顺序选择不与已批准事务冲突且 donor 未占用的首项。

Fit 先检查实际新坐标的最小角，再取旧见证的有理误差向下取整为微像素并减 10000。浮点线性半空间裁剪只生成候选；写入 double 后，区间或有理计算复核全部可见闭补丁。误差检查数统计不包含几何谓词中的全部有理调用。`fit_bound_failed/numeric_unknown/fast_shape_miss` 不解释为整个操作类别不可行。

## 4. 局部发布与失败保证

Apply 校验版本、认证标记、面数增减、R/W 冲突及活动预算。对本批旧支持合并点邻接和边邻接；外接口未变化的邻面从旧记录继承。不对 live 状态边发现边尝试。

准备结构为局部 `std::map` 和向量；新身份索引及新边树节点预分配。容量扩张在逻辑发布前完成；失败可能保留容量变化，但旧逻辑状态、版本和预算不变。随后用 node handle 转移和已有容量写入更新存活记录，推进一次代际。没有正常全域状态重导入/排序/复制，也没有逐项 split replay。

几何 R/W 使用面、边关联、读写高度；共享派生点邻接由整批私有合成后串行一次写入，不能把它理解为并发执行任意 `Apply`。真正多线程尚未实现。

## 5. 构建、费用及已知差异

`PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD=ON` 显式启用，要求 `PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE` 且 `BOOST_VERSION=109000`。库与探针/测试分离；Input/Validation 不进入正常核心库。核心禁用 fast-math/浮点收缩；Boost 精确数值只在相关实现文件可见。

定位为格点包围框枚举，并非每面扫描全部 Q；本轮保守外扩范围带来额外初建访问。相机/Q 全量初始化、输入 JSON、独立全量验证成本已单列。没有跨批证书缓存、增量 sample ownership、屏幕面积加权 RMS、Pending、mesh 消费或动态参考；这些为 PLANNED，不是当前代码事实。

当前 API 的 `CertifiedBatch` 是模块内受信任值；结构发布再次验证不能替代对任意外部篡改几何的质量重认证。CLI 不接受外部批次。未来公共生产入口若暴露批次，需要重新审查封闭性。

## Unresolved / Uncertain

已确认当前单批数据所有权和调用关系。UNCERTAIN：持续状态下正常更新成本、长期视域恢复、缓存/网格消费者正确性和真实多线程收益，当前无实现或测量；不能从本文件推出已成立。
