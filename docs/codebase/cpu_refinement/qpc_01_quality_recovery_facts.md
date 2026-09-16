# QPC-01：有限质量追溯实现事实

2026-09-16。仅描述[QPC-01](../../plans/cpu_refinement/qpc_01_quality_recovery_audit_plan.md)新增观察路径及直接依赖；生产算法事实沿用PQ-05/GWR/TPI，不在此重复整个模块。

## 1. 文件与依赖

| 文件 | FACT：职责、调用者与所有权 |
|---|---|
| `src/benchmark/experiment/TransactionalRecoveryTrace.h/.cpp` | CPU研究入口编排；读取冻结ReplayInput和有限见证，拥有executor、pipeline、observer与输出文件 |
| `src/benchmark/experiment/ExperimentCpuMain.cpp` | 精确匹配`--recovery-trace`；其他调用仍交原RunExperimentCpu |
| `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h/.cpp` | 只读观察、局部反事实和恢复解释；新增显式列表/真实seed/全部覆盖根，不参与Plan选择 |
| `tests/CMakeLists.txt` | 上述两个实现编入既有`parallel_roam_experiment_cpu`；原PQ目标继续单独编入provenance |
| `scripts/run_transactional_recovery_trace.py` | 冻结选样、来源校验、组件分组和有限原生采集 |
| `scripts/analyze_transactional_recovery_trace.py` | 独立读取raw，核对FER身份，归约见证事件与生成科学图 |

FACT：没有改动生产`src/algorithms/greedy_transactional_lod/`、公共renderer、Registry或质量评价器。应用目标没有链接新观察器。实际依赖仍为`benchmark → experiment → algorithms`，Python只负责运行/归约，不计算生产决策。

## 2. 输入、输出与生命周期

显式命令：

```text
parallel_roam_experiment_cpu --recovery-trace RESOLVED WITNESSES OUTPUT
```

`RESOLVED`由原`LoadReplayInput`校验source/camera FNV、维度、viewport和共同设置。追溯要求algorithm=`transactional`、immutable、flip recovery开启；没有重新解释terrain scale、threshold、budget、prefix、矩阵或camera recipe。

每行见证为`sourceFrame returnFrame ordinal u v`，按sourceFrame组成WitnessGroup；一个组共享未来和返回投影，至多六行、有限单位UV、帧范围合法、同组不重复UV。ordinal作为来源记录，C++曲面定位使用UV，Pythonselection保存完整样本身份。禁止空表和覆盖已有输出；检查在种子构造前完成。

`WitnessGroup`仅持有两帧索引与Point列表，生命周期为本次进程。`BoundaryGeometry`保存域外边的稳定身份集合与端点Point映射，只读比较，不是生产状态副本。

控制流：

```text
LoadReplayInput → ReadWitnesses → 拒绝已有输出
  → SeedBuilder.Build(第0视图，只调用一次DOD)
  → 原Pipeline.Initialize → 原StateInvariant.Validate
  → Boundary seed + 各观察器Seed
  → 每机会：SetView → Reservation.Plan
       → Before(只读已冻结batch)
       → Pipeline.Apply → ConsumeMesh
       → budget / counters / boundary比较 → After
       → 原ValidateReplayMesh + frame ledger
  → 末态StateInvariant.Validate → RAII释放
```

FACT：frame0仍执行一次真实Plan/Apply，和公共adapter一致。`Seed()`在它之前单独记录，不把frame0或frame2当初始种子。固定未来/返回Configuration只传给观察器，不写入运行pipeline。

输出包括顶层`frames.csv`与`boundary.csv`，以及每个`frame-<sourceFrame>`目录的`witnesses.csv`、`transactions.jsonl`、`recovery.jsonl`、`roots.jsonl`。原始文件写入失败抛出，入口返回非零，外层runner保留错误/上限记录。

## 3. Observer扩展与历史兼容

FACT：旧optional追加见证构造函数保留。新vector构造函数复用旧文件初始化后替换`_points`、调整`_expected`，设置`_explicitWitnesses`并打开`_roots`。旧默认模式不生成roots文件，旧CSV列顺序不变。

- `_points`：冻结UV，不随面槽回收改变。
- `_expected`：Before读取旧曲面，按已选局部替换预测；After与实际内部高度比对，容差`1e-8`。
- `_survivors`：Before记录全部活动顶点高度，After检查仍存活身份高度严格不变。
- `_future/_returned`：只读投影配置。
- `_roots/_explicitWitnesses`：只控制显式诊断输出，不影响生产状态或计数。

`CoveringRoots`按参数域AABB排除不可能命中，再调用已有Contains；全部命中按稳定面Id排序。CSV仍用最小Id owner保持单点曲面口径，Recovery和roots则覆盖全部命中，避免共享边遗漏。

`WriteRoot`记录面Id、rank、priority、threshold、三个端点几何、三边关联数、当前可见样本数/最大误差。rank沿原全序`(-priority,stableID,slot)`；非有限或未严格过阈值返回0。当前冻结数据的这些priority均有限，归约把rank0的outside_prefix细分为`below_threshold_or_nonfinite`；本轮实际均为低于阈值。

`Recovery`复用历史实现：

1. 不在IntentIds则标记outside_prefix；归约结合rank区分未过阈值。
2. 在prefix则输出完整Attempts和IntentResult；本根已选直接记录。
3. 翻边已认证但未选记录flipConflict。
4. 普通已认证而未选：用独立WorkLedger重建首个真实认证receiver，只看更高priority获批项的占用，按既有named credits或冻结donor pool逐层解释。
5. 若发现无理由未执行的available exchange，抛异常而不悄悄归类为冲突。

FACT：额外重求值deadline60s、VisitLimit沿旧诊断设为100000000；生产本批WorkLedger没有混入这些计数。若异常/资源截止发生，trace未完成并由runner记录，不能解释为无解。本轮无此退出。

显式模式的`witnessGeometry`只为被选事务覆盖的见证展开旧面、新面、源高度、新点初始/拟合高度、未拟合反事实和样本支持。默认旧模式仍沿历史条件输出geometry；两见证文件实测逐字节相同。

## 4. 生产语义的必要关联

FACT：`ReceiverCursor::Next`中E只接受Count=2；F的误差点需StrictlyInside和三个正orientation，否则仅保留内部重心；H跳过IsBoundary点。Prepare在immutable时Free只含新点。

FACT：`TransactionalSamples::BuildOrders`和局部修复排除Boundary donor；Donor保留环上Point，只删内部center。`TransactionalFlipRecovery::Construct`需内部双侧边，保持四旧点，替换内部对角线。

INFERENCE：在当前生成/提交路径、合法seed和immutable前提下，每种事务都保持域外边界线段及端点几何，故跨批外边界分段高度不变。它是本轮有限代码层论证，不是新Lean证明，不涵盖未来新增原语或外部伪造Proposal。

FACT：priority平方为`max(当前可见sample误差平方,.04*最长投影边长平方)`；只有有限且严格超过split阈值平方才进入候选索引。DOD独立质量在同点更好不自动要求本策略继续生成请求。全域可见Q最大误差、见证误差、priority三者不能混用。

## 5. 离线样本与归约

FACT：`locations.csv`并非全Q。`sampling_coordinates`以float栅格UV构造固定两三角cell顺序，利用首次出现的顶点/规范无向边位置，在每三角内部按vertex、edge交错后发出centroid。所有保存ordinal位置逐条相同，并有三个小矩形网格的独立逐项枚举对照。

选样读取完整`errors.f64`，核对共同source/Q/view、文件SHA、样本数和可见性mask。阈值`.25px`仅用于本次分组，不是人眼接受门槛。raster cell八邻接BFS标签及所有簇均保留；选最大样本簇、最大excess簇及Emax代表，不做形状可修性筛选。

归约首先核对完整轨迹，失败就停止；再按稳定UV的每帧曲面构造事件链。geometry-events只保留高度实际变化>`1e-10`的事件，未变化也保留在完整raw transactions/witnesses中。图表是参数域分布和固定投影时序，未生成新terrain或renderer截图。

## 6. 成本、线程与边界

FACT：真实Plan/Apply复用原8线程CpuTaskExecutor；observer在batch封闭后主线程只读运行，无新增并发写。强行在热路径启用本诊断会引入全扫，不能把它当免费instrumentation。

INFERENCE：设见证数w≤6、活动面N、边E、活动顶点V，覆盖定位/排名/旧点检查每机会至少涉及`O(wN+E+V)`次遍历；map访问及精确谓词另计。局部恢复额外成本随覆盖根、donor pool、prior footprints和样本认证规模变化，并非O(1)。全部属于诊断费用。

Python全Q坐标重建有排序/unique成本`O(q log q)`及`O(q)`内存；cell连通扫描`O(width*height)`。它只跑一次离线选择，不改变未来runtime复杂度。当前两个脚本分开承担采集和分析，避免报告/绘图进入原生入口。

UNCERTAIN：有限见证不覆盖全部广泛退化区域；本阶段没有一般恢复能力保证，也没有确认新原语可以低成本修复。PLANNED：后续质量设计须用户验收后进入对应小阶段。

## 7. 构建与验证事实

此工具借用tests支持库。既有CBT-on项目级宏会让CPU Registry引入D3D12/SDL，本次CPU研究构建关闭CBT runtime选项，保持生产CPU核心和原矩阵；未修复这一既有构建组合问题。实际构建配置/失败日志留在raw，末尾恢复原cache选项。

240机会与FER精确对应、原PQ默认文件兼容、七项错误输入拒绝、普通前后费用和源SHA核查见[报告](../../research/cpu_refinement/qpc_01_quality_recovery_results.md)。没有全量测试或无关backend重建。
