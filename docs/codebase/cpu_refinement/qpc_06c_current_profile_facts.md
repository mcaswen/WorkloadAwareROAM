# QPC-06C：当前热点观测与执行事实

2026-09-17。本增量事实覆盖当前profiling入口、两处新增区间、归约工具，以及报告中定位的直接算法调用链；不替代已有Transactional全模块事实。关联[规划](../../plans/cpu_refinement/qpc_06c_current_hotspot_profile_plan.md)。

## 文件、依赖与所有权

| 文件 | 实际职责/状态 |
|---|---|
| `src/algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.cpp` | FACT：Enabled之后增加Certify/ValidateBatch两个静态区间；原有质量证书、控制分支、数值式不改 |
| `src/profiling/CpuProfiling.h` | FACT：复用既有开启/关闭宏；算法不读取profile数据，不引入实验反向依赖 |
| `scripts/experiment_infrastructure/profile_adapter.py` | FACT：既有prepare/采集/导出入口；本次把临时捕获放`~/.cache/roam-profiling/runs`，复制CMakeCache/compile_commands到产物，无算法逻辑 |
| `scripts/analyze_transactional_current_profile.py` | FACT：读取原始结果、选择帧、核对逻辑/工作、归约全部符号/调用边/栈、线程时序和绘图；不启动被测程序、不修改输入 |
| `scripts/profiling/report.py`、`tracy_report.py` | FACT：复用现有perf样本、ROI及Tracy区间解析；没有重写捕获协议或另建profiler |

新增工具的记录只存在离线Python内存与报告文件。C++区间为函数栈生命周期；没有给State/Proposal增加profiling字段，没有每sample事件。Windows正常运行宏关闭；Linux两种观测构建保持原算法适配器与数值编译约束。

## 执行与状态边界

FACT：`TransactionalExecution::Run`只生成`min(count,Workers)`个商余连续块，局部WorkLedger归块所有，记录task编号；Dispatch返回后主线程Merge账本，再检查全批限制。Diagnostics线程集合是另外的观测字段；生产配置不因此次捕获而改变。160个receiver根形成8个20根块，块内串行。

FACT：`TransactionalReservation::Plan`持有按prefix索引预分配的certified/attempts/result数组。各worker消费独立索引；`ReceiverCursor::Next`生成下一目录项，旧`CertifyReceiver/Fit`先执行，旧成功且P启用才执行新`PointwiseQuality::Certify`。失败继续目录，有限条件下进入原flip恢复。join后按同一全局前缀收集，worker完成次序不决定reservation次序。

FACT：存在D_need才对共同donor池创建并认证提案；本次Sierra移动段Need为零。Canyon的donor构造后同样可以调用逐点认证；故`gtp.pointwise.certify`调用总数包括两个角色。后续仍是原footprint/预留流程，本次未修改。

FACT：`TransactionalQualitySampleEvidence`拥有一个sample/domain内的reference、坐标与投影证据；`CoverPrepared`逐面逐边检查区间定向，模糊时构造精确边式。面端点的有理转换没有统一预备成可复用的提案面记录。旧`Samples::Weights`先走有误差界的double过滤，不确定时才进入有理分支。`TransactionalProposalEvidence::Get`的weights缓存属于单提案，不能当作新双域完整证据。

FACT：Pointwise::Certify分别在核心/输出域检查接口，收集关联或包围盒候选、排序去重、定位旧新覆盖面、比较样本损伤/累计进展。成功后才发布QualityProof；ValidateBatch保留版本/内容核对与共享支持义务。新增区间没有省略这些步骤。

FACT：Samples::Prepare负责事务后owner、闭面贡献、受影响分数/索引的准备；并非仅写入新增面的常数操作。PrepareView仍遍历Q与候选；此次没有改生命周期或数据表示。

## 归约方法与验证

FACT：分析器按warmup=0/event=reveal取得frame3～31，使用perf ROI窗口定位样本；以period加权，每个inclusive函数每栈计一次、每条调用边每栈计一次，保留全路径。自身权重和必须等于ROI总权重。CSV保存所有非零inclusive符号，未截成Top-50；全路径以gzip存储。

FACT：Tracy先将区间归入profile.frame，再按线程构造嵌套关系扣直属孩子得到self时间；worker任务通过dispatch时间包含及phase标签关联。task总时长/最长块/真实线程/wait/尾部独立输出；同线程交叉嵌套及负self触发检查。区间值是wall duration，不是CPU time。

FACT：跨模式以frame、相机/投影、hash、预算/面数、序列、逻辑人口和真实工作/写入字段核对；不以CPU时间或Linux没有的上传字段判等。四组96帧与对应Windows06B行为相等。summary保存来源SHA；工具打印离线归约耗时。

INFERENCE：当前接收完成时间受连续块中最大总成本限制，属于实现划分，不是ROAM必需span。共享read-only输入与索引收集支持研究更细任务，但尚未验证配额、异常、ledger、内存和执行费用，不能把可细分直接写成性能保证。

INFERENCE：proposal-local面证据可能减少多sample重复转换/谓词工作；有理精度、准备和内存成本未实测。上述仅为候选，不是已存在组件。

## 符号与路径索引

- 观测：`PointwiseQuality::Certify → gtp.pointwise.certify`；`ValidateBatch → gtp.pointwise.validate_batch`。
- Receiver：`Reservation::Plan → Execution::Run → ReceiverCursor::Next → CertifyReceiver/Fit → PointwiseQuality::Certify → prefix-order collection`。
- 数值：`Certify → QualitySampleEvidence::Cover → QualityEvaluation::CoverPrepared → interval / rational`；旧路径`Fit/Measure → ErrorBounds/ProposalEvidence::Get → Samples::Weights`。
- 工具：`profile_adapter.collect → ProfileSession/perf或Tracy → 原始产物 → analyze → comparison/perf_details/tracy_details/plot_report`。

## Unresolved / Uncertain

- UNCERTAIN：没有原生Windows函数比例和硬件cycles/cache观测；跨系统行为相等不消除此限制。
- UNCERTAIN：任务区间尚未按每根拆分；最长根成本、可获调度收益和面证据缓存净收益仍需独立消融。
- FACT：Sierra L样本1811，低于建议2000；未知外层祖先与少数未知自身保留，不生成缺失符号的猜测归因。
- FACT：本轮只补观测，没有新的质量、恢复或速度保证。
