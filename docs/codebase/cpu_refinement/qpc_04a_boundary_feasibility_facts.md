# QPC-04A 外边界调查代码事实

日期：2026-09-16。范围仅为本阶段只读诊断及其直接入口，不是生产边界原语已经接入的说明。相关[规划](../../plans/cpu_refinement/qpc_04a_boundary_feasibility_plan.md)与[结果](../../research/cpu_refinement/qpc_04a_boundary_feasibility_results.md)。

## 1. 文件与依赖

- **FACT** `src/experiment/greedy_transactional_lod/TransactionalBoundaryAudit.h/.cpp` 定义静态诊断 `TransactionalBoundaryAudit`，依赖生产 State/Samples、Predicates、Certification、Proposals、Reservation。没有可变生产所有权，不调用提交或复制全状态。
- **FACT** `src/benchmark/experiment/TransactionalRecoveryTrace.cpp` 复用原冻结输入/种子/持续轨迹，在额外参数 `--boundary-audit` 明确启用时调用；每个见证组仅在其来源机会、SetView之后、Plan之前观察。原 Plan/Apply、预算/完整边界不变断言及输出均保留。
- **FACT** `tests/CMakeLists.txt` 只向实验 CPU 目标和解析夹具编入观察器，采用严格浮点编译；公共应用、算法适配器和两个渲染器不依赖它。
- **FACT** `scripts/run_transactional_boundary_audit.py` 负责固定三输入、仅旧第一见证、进程调用、身份/轨迹比对、费用归约和图。原生运行沿共同helper，无另一套相机或实验schema。
- **FACT** `scripts/transactional_recovery_geometry.py::validate` 增加可选 `boundary_split`。默认仍要求原边界有向边集合完全相同；显式模式仅允许一条单位域单侧边变成同向两段。

## 2. 构造与诊断数据流

`Construct(state, root, edge)` 规范边身份，要求关联为一面且属于root，端点位于同一单位域外边界。仅复制原面三个点；私有新点使用未消费的 `NextVertexId`，UV与高度取端点均值。用另两条有向边连接新点，得到两个面；旧边被分段，`Support={root}`、`Free={newId}`。这里只声明新点写入，不运行高度拟合。

失败使用 `not_single_sided_root_edge`、`not_domain_boundary`、`midpoint_not_distinct`，不发布状态。构造层不承担完整初态合法性证明，调用入口先沿原 StateInvariant 验证种子。

`Run` 的流程为：

```text
读取原全需求排序和活动面
→ 对冻结见证枚举全部闭包含根
→ 单侧边去重、稳定排序
→ Construct
→ EvaluateVariant(linear)
→ EvaluateVariant(source)
→ 原轨迹继续 Plan/Apply
```

`AssessVariant` 设置可见支持，调用原Shape、SetProgressTarget、Measure/Accepts；独立记录当前支持最大误差、逐点最大退化和位置。`EvaluateVariant` 管理单个私有高度版本、JSON和费用；源高来自原U16双线性场，未来投影未作为输入。预算不足时 `BudgetPair` 沿原 `DonorPool` 顺序调用既有 Donor/Accepts/Footprint/Conflict，首次合格即止；不模拟整个贪心批次。

源高与线性提案共享几何支持；旧点不变。一次候选求值完整结束才输出一行，资源/异常写独立 `unknown`，不将未知归为不可行。每次Run最多六条边、两种高度、200万样本访问、60秒；最多原池大小。调用者仍受180秒进程限制。

## 3. 输出和生命周期

输入状态/样本均为const借用，只在当前快照有效；局部map、Proposal、Footprint、排序副本和JSON流由本次Run拥有并销毁。没有持久缓存、跨帧策略或并发写。

`boundary-audit-<frame>.jsonl` 保存frame/root/edge/rank/prefix、预算与面数、旧/新有限几何、样本数、接受原因、微像素目标、旧最大/新上界、原见证和新增退化见证、有限捐赠配对、样本访问与时间。末尾汇总候选数和完整诊断费用。输出路径已存在时拒绝覆盖。

脚本独立验证六个几何版本；原三轨迹 `frames.csv` 与 `boundary.csv` 逐字节对照，正常前后版本使用相同哈希、面数及工作量字段对照。费用按原 `cold=0 && warmup=0` 的21机会归约，进程仍是统计单位。图和归约属于离线处理，未进入生产计时。

## 4. 已确认的接入缺口

- **FACT** 当前生产 `TransactionalCommit::Prepare` 仍要求普通接收方 `Faces=Support+2`；翻边单独净零。调查的 `+1` 记录没有进入生产 `CertifiedBatch`。
- **FACT** 当前 `TransactionalReservation::Plan` 用 `(B−N)/2` 命名空额度；原追溯和部分续接验证据此断言 `N_next=N+2·FreeExecuted`。
- **FACT** 当前提交器新建 `VertexRecord` 的 `Boundary` 保持默认false。旧原语只创建内部点，因此原行为自洽；未来边界新点必须明确设置，不能只放松面数检查。
- **FACT** 原捐赠索引排除边界点；未实现外边界回收。新边界点的长期预算/生命周期是未来设计义务。
- **INFERENCE** 单侧中点几何构造为有界工作；整个候选成本仍依赖样本数和捐赠搜索，不能称常数时间恢复。
- **PLANNED** 按1/2实际面数预留、新点边界续接、公共策略开关、目录顺序和实际float输出验证，仅作为结果报告里的接入草案，当前未实现。

## 5. 验证与限制

新C++夹具覆盖四向边界、双侧拒绝、错误根、洞口、旧点和分配器不变、最小角等号/更小角。共享Python检查继续覆盖PQ-04默认行为，并增加显式分段及非法域边/偏离边界拒绝。

自然结果基于三个冻结输入、两个不同局部几何，生产轨迹完全未变。新增诊断不能作为持久队列、样本修复、法线、Pending或并行提交已经正确的证据；这些仍属于后续生产接入。

未发现反向依赖或生产热路径新增调用。观察器全域定位与读取全排序只为有限诊断，不能移植到生产原语并沿用其成本主张。
