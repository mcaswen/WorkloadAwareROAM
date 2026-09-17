# QPC-06D：接收根动态领取代码事实

2026-09-17。范围为`TransactionalExecution`、接收阶段调用和同步CPU执行器直接边界。关联[小规划](../../plans/cpu_refinement/qpc_06d_receiver_task_balance_plan.md)，不重述不受影响的几何/质量模块。

## 文件与依赖

- FACT：`src/algorithms/greedy_transactional_lod/TransactionalExecution.h/.cpp`增加`RunIndependent`。只负责独立索引分配，仍调用原Run；不读取State/Samples、不筛选或排序候选。
- FACT：`TransactionalReservation.cpp`仅将`receiver_stage`切换到该入口。根内部目录、Fit/逐点认证/翻边路径、预分配输出及屏障后按前缀收集均未改。donor、View和写入继续使用原Run。
- FACT：`CpuTaskExecutor`、`CpuThreadPool`无修改。同步派发数仍是`min(count,Workers)`，没有按根数创建线程或追加线程池任务。
- FACT：新增`tests/TransactionalExecutionTests.cpp`及CMake目标单独验证执行协议；没有给生产入口增加实验开关。
- FACT：`scripts/analyze_transactional_task_balance.py`复用06C的结果/采样/时序解析，新增前后时间分布和根区间分析；不调用生产C++或决定调度。

## 控制流、数据所有权与内存顺序

FACT：单线程或至多一项时RunIndependent直接调用Run；串行回调可以一次收到完整区间。多线程时在同步栈上建立`atomic<size_t> next`，借原Run发出有限任务。每个任务循环`fetch_add(1, relaxed)`，索引超界即退出，否则检查本地配额并调用`task(index,index+1,local)`。

FACT：原子只承诺唯一索引，业务内存同步由既有Dispatch和排空边界负责。局部账本由外层同步任务独占，不随根重置，不生成r份map。共享输入只读；输出仍按原prefix索引写入不同元素。所有已派发任务结束后，原Run合并整数计数、Reasons、最大有理位数及局部时间，再CheckLimit。

FACT：`gtp.task`仍表示外层同步任务；开启Tracy时新增`gtp.item`并记录`phase/index`，表示一次独立领取的根回调。诊断Execution三元组仍表示输入项数、外层派发任务数和调用任务线程数，不把根数写成线程数。关闭Tracy时没有字符串标签或根区间记录。

## 失败、配额与持续状态

FACT：任务异常仍交给CpuTaskExecutor按原协议保存并排空后传播；RunIndependent的原子与借用回调在这之前不离开作用域。部分入队失败仍由原执行器停止实例并排空。没有异步返回、后台队列或跨帧调度状态。

FACT：WorkLedger对SampleTouches和Deadline检查；每任务继承同一配额，且总账本仍再次检查。细粒度领取不将VisitLimit变成每根独立额度。总量超过限制但各局部尚未超限时，合并检查拒绝整个批次。

FACT：多异常/期限失败时，最先报告的逻辑根和部分诊断人口可受调度影响；原接口只保证排空后传播，不承诺逻辑根顺序。正常完成结果必须一致，失败不得发布部分生产状态。

## 同结果推导与复杂度

INFERENCE：在无异常、期限和配额不触发的运行中，每根仅写独占位置，领取全集恰等于原前缀。根内控制与数值式不变，join后的输出数组仍逐项一致；原收集/预留不观察完成顺序，所以批次与持续结果不变。并行任务时间不是确定逻辑输出。

INFERENCE：新增领取操作O(r+p)，调度内存仍O(p)；几何/认证工作没有理论减少。固定块最大总成本被根级列表调度替代，最长单根仍不可分。原子、时间检查、缓存与根调用边界均有真实成本，是否降低完整时间只由有限对照判断。

## 验证入口与符号索引

- `TransactionalExecution::RunIndependent`：串行复用；并行原子领取；只借用Run。
- `TransactionalExecution::Run`：原有派发、局部账本、Merge、总配额和诊断。
- `TransactionalReservation::Plan`：仅接收准备选用独立入口，屏障后继续原全局收集。
- `TransactionalExecutionTests`：0/1/不足线程数/多根唯一性与结果、四线程会合、异常排空/复用、局部及总配额、期限。
- 既有`GreedyTransactionalLodTests::ParallelExecutionAndFailure`：持续状态、串并行候选/工作/端点、准备失败不发布、视图生命周期。
- 离线：`compare_timing`按事件前后对比；`item_details`按线程/父任务归属根；采样仍交给06C解析器。

## Unresolved / Uncertain

UNCERTAIN：最长根、内存带宽和资源竞争仍可能限制收益；本修改没有减少逐点认证所需样本，没有降低任何质量义务。没有证据说明所有前缀规模/机器都受益。跨平台性能须分别看本阶段报告，不沿用06C的模型敏感性作为收益。

## 本阶段观测与未决项

FACT：有限运行结果见[报告](../../research/cpu_refinement/qpc_06d_receiver_task_balance_results.md)。原生P移动完整均值下降40.84%/31.43%；Linux复现同方向；前后逐帧导出逻辑、输出与整数工作一致。任务和基本不变，最长任务约减半，不能表述为几何工作减半。

FACT：`TransactionalPipeline::Update`空批命中发生在Reservation之前；命中时不执行新入口。既有CSV里Canyon P恢复段的30个无视图/接收/样本修复及写入机会仍有小幅时间差，见[分析](../../reviews/cpu_refinement/qpc_06d_idle_timing_analysis.md)。UNCERTAIN：没有确定其版本因果关系，不能凭未修改该代码就宣称无间接性能影响。
