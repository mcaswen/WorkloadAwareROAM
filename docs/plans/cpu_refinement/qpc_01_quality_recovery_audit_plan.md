# QPC-01：质量来源与恢复阻塞联合审计

日期：2026-09-16；基线`01c0583`。用户授权本阶段自主完成，完成后提交结果并交用户验收，**不自动进入QPC-02**。本阶段为诊断实现，不改变生产质量、预算、目录或选择规则。

## 1. 目标与出口

承接[QPC大规划](transactional_quality_performance_closure_major_plan.md)的V01/V02/V08，回答：Peking同一边界点为何跨预算不恢复；峡谷、山地的广泛逐点退化来自哪里；空预算为何没有转化为有效恢复。

输出每个预定见证的“种子→几何变化→当前/固定未来投影→priority/rank→目录/认证→预留/执行”链。区分来源已定位、机制已证实、只有相关性和仍未知。不以本阶段修低Emax为通过条件，不提前选择QPC-04修复。

已阅读开发/规划/审查规范、PQ-02/05和FER-02证据；已扫描Provenance/RecoveryAudit、ReceiverCursor、Reservation、SeedBuilder、公共adapter、ExperimentReplay/Case/CameraSequence与相关CMake。当前外边界操作能力是待验证重点：E只枚举双侧边，F在面内，H跳过boundary中心；固定旧点下外边界折线可能不变，须以完整原语审计与真实轨迹验证，不能仅凭最大值相等下结论。

## 2. 冻结输入与选样规则

- 复用FER-02四资产及原始Q、实际mesh、locations、误差数组和normal records；不换高度比例、相机、预算、阈值、prefix或线程。
- 实际追踪只取Peking50k、Peking200k、dem-canyon50k、dem-sierra50k，全部原D3D12 ZO矩阵、immutable＋flip、8线程；24/96机会不增轮数。输入直接使用FER冻结resolved/camera/source，经原LoadReplayInput核对，不重写相机公式。
- 第一见证固定为各问题帧原Emax：Peking15、峡谷48、山地48及95。Peking50/200k使用相同UV。
- 用已有全误差数组对T−DOD>0.25px的样本分簇：映射到原始raster cell，边界clamp到末cell，按占用cell的8邻域连接。它是诊断分组，不是严格连续误差区域。分别选最大样本簇和包含最大逐点超额的簇，各取簇内最大超额样本，平局取最小ordinal；重复UV合并。规则在重新执行前冻结，每场景最多6个见证。
- 山脊只做既有数据分布/流失对照，不另加96机会追踪。各簇数量、大小、选择与未选范围完整保留，不把少数见证说成覆盖所有退化。
- 见证跨帧使用UV，不使用物理slot；共享边/顶点记录全部覆盖根，不能只凭一个tie-break owner断言未进入前缀。固定未来/返回投影仅用于诊断。

## 3. 文件归属与接口

| 方式 | 文件 | 职责与原因 |
|---|---|---|
| Extend | `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h/.cpp` | 加显式有限见证列表入口，保留旧默认两点/追加点格式；记录覆盖根几何、优先级证据和恢复链，不改正常选择 |
| Create | `src/benchmark/experiment/TransactionalRecoveryTrace.h/.cpp` | 用已有LoadReplayInput、SeedBuilder、Pipeline执行冻结case，编排Before/After、边界不变量和hash；与历史硬编码PQ入口分开 |
| Extend | `src/benchmark/experiment/ExperimentCpuMain.cpp`、`tests/CMakeLists.txt` | CPU研究工具新增显式`--recovery-trace`入口，复用已有实验依赖；公共应用和renderer不链接新观察器 |
| Create | `scripts/run_transactional_recovery_trace.py` | 有限选样/来源冻结与原生诊断启动；复用runner/hash/native启动能力，不复制质量公式 |
| Create | `scripts/analyze_transactional_recovery_trace.py` | 独立核对FER轨迹与归约因果链/分析图，使离线报告职责不堆入采集入口 |
| Create | 本规划及对应research/facts/review、精简data/必要图 | 分离意图、执行证据、真实实现与审查；原始大文件进入忽略目录 |

依赖保持`benchmark → experiment → algorithms`。新入口只接受已有resolved schema和简单见证表（ordinal、UV与来源帧），不建立新的通用场景系统。使用当前核心真实Plan/Apply；不dry-run Legacy获得中途目标，不从未来网格覆盖真实状态。

## 4. 实验与实施步骤

1. 保存生产源文件哈希及当前实验CPU/provenance程序；如CPU工具尚未构建，先以未改源码只构建该目标。用Peking50k执行一次原CPU正常回放，记录完整进程费用与24帧hash/账本，核对FER；不把无renderer费用当平台CPU-ready。
2. 离线按§2选簇，保存输入SHA、完整样本身份、阈值与选中见证。检查原第2机会和问题帧同点高度；这一步可以给出种子/后续变化线索，但frame2不能冒充真正初建种子。
3. 实现显式观察器与通用case诊断编排。初建后记录真实seed与边界折线；每轮在batch冻结后只读观察，再Apply原batch，核对局部预测、固定旧点、边界集合/高度和完整预算。
4. 对进入前缀的覆盖根记录实际Attempts/IntentResults/选择；必要的失败预留解释使用独立诊断账本且设置配额。未观察完写UNKNOWN，不以跳过诊断作不可行。记录完整普通/翻边计数及样本工作量。
5. 先新工具正常Peking50k验证旧入口不变，再运行四个冻结轨迹的追踪。每条与FER逐帧hash、N、receiver/need/feasible/exchange/free/pair/conflict/reuse和flip核对；不一致先定位环境/输入，不继续写因果结论。
6. 对实际曲面变化做transaction provenance；对一直不变的边界点，结合所有现有原语的外边界作用写限定的归纳论证。记录面角度与形状拒绝、阈值/前缀、预算/冲突的不同阻塞层，不扩展新操作搜索。
7. 输出有限见证时序/误差簇图并查看，报告实际mesh质量与内部诊断精度的差异。完成事实、审查、总规划状态与本页结果后独立提交，停下交用户验收。

## 5. 验收与性能边界

- 原生产算法源码不变；原CPU入口同条件hash与工作量不变，新trace额外计算不改变批次或mesh。原PQ默认观察接口/格式保留，定向兼容对照优先复用旧二进制/产物。
- 参数校验：见证数上限、有限UV、来源帧范围、空表、未知参数、输出覆盖拒绝；错误输入在种子/运行前拒绝。只做必要的输入检查和独立来源对照，不跑全CTest。
- 完整诊断明确记录首次变化和无变化；内部高度/投影与实际float输出对照，差异有界但不要求不同精度逐bit相同。边界几何不变通过每帧集合/身份/高度检查，不能仅靠hash碰巧相等。
- 修改前后各一组相同Peking50k普通CPU进程费用对照；诊断费用单列。生产renderer未改、未链接观察器，复用FER平台证据，不重跑平台矩阵。费用筛查按开发规范§7.3，明确风险才至多一次定向复测。
- 原生进程每条180秒/8GiB，诊断局部重求值单项60秒和既有VisitLimit；四条完整追踪及兼容检查目标5分钟内，构建/离线聚类另计。失败/超时保留，不自动增资源重跑。
- 本阶段只提出后续质量修复的证据需求及操作点，不修改“不明显退化”的阈值，也不宣称完成持续质量修复。

## 6. 结果与用户验收

状态：**已完成诊断闭环，等待用户验收；QPC-02未开始。** [结果报告](../../research/cpu_refinement/qpc_01_quality_recovery_results.md)、[实现事实](../../codebase/cpu_refinement/qpc_01_quality_recovery_facts.md)、[架构审查](../../reviews/cpu_refinement/qpc_01_quality_recovery_review.md)已经回填。

- 选样冻结2/1/3/5条见证，四轨迹共240机会；mesh hash、面数、候选/预留工作量、样本触及与flip全部对应原FER。没有生产核心修改，输入、预算、阈值、相机、prefix不变。
- Peking/峡谷所选外边界高度从seed起始终相同；完整边界集合和几何每帧核对。已有原语外边界保持论证成立于明确前提，非Lean新证明。
- 山地frame48最坏点存在两次新点拟合变化，固定未来误差1.987960→2.026711→2.015012px；之后无恢复。其他内部代表点的问题帧全部覆盖根低于4px门槛；共享边的全部根分别记录。
- 误差簇全量输出，但少数代表不替代全域归因。山脊只复用FER分布/流失，无新增轨迹；未推进质量修复或重放更多恢复批次。
- 四条trace总49.67秒，无上限退出；原默认PQ双见证三文件逐字节相同。7项输入拒绝检查、3个独立小采样域和实际Q已保存坐标核对完成。
- 普通CPU前后各一进程24机会，暖均值39.1208→38.5578ms；仅作为未见退化筛查，不作性能提升主张。所有诊断全扫费用另列，无全CTest和平台矩阵复跑。
- `locations.csv`实际为稀疏记录，因此采集前修正了全Q坐标重建方式；没有改变选簇规则。两个未来投影组允许记录同一UV，其他重复在组内合并。
- 原生tests原先关闭；开启后暴露既有CBT-on CPU Registry链接依赖，本轮CPU构建临时使用`PARALLEL_ROAM_BUILD_TESTS=ON, PARALLEL_ROAM_ENABLE_CBT_2024=OFF`。应用未重建，诊断后恢复原缓存。该组合问题留痕，不纳入质量审计修复。
- 原始产物在`benchmark-output/cpu-refinement/qpc-01/run-01`并沿既有规则忽略；精简表/JSON/两张必要图进入research data。采集与报告分成两个脚本，属于同一批准职责边界的文件细分。

此阶段提交后停下；不因“诊断完成”启动QPC-02或提前实现QPC-04。
