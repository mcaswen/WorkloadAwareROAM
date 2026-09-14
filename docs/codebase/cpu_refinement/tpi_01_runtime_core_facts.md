# TPI-01 运行核心与执行工具事实

> 2026-09-15。范围是本次迁移和执行依赖，原算法内部事实继承[接入前事实](transactional_platform_integration_baseline.md)；不把后续适配器写成已实现能力。

## 核心与研究分界

**FACT**：`src/algorithms/greedy_transactional_lod/` 的命名空间为 `ParallelRoam::Algorithms::GreedyTransactionalLod`。26 个文件的内容除 include 路径与命名空间外，与 `c7e6cd1` 逐字相同。

| 文件组 | 真实职责与依赖 |
|---|---|
| Types.h | 稳定身份、几何、冻结配置、工作账本、提案与批次值类型；标准库 |
| PriorityIndex.h | 精确块前缀索引，依赖 Types |
| State.h/.cpp | 唯一可写一般网格状态、稳定身份与槽关联；Types |
| ViewState.h/.cpp | 视图派生的暂存/发布值；Types |
| Samples.h/.cpp | 原始参考、固定 Q、样本归属、评分、前缀与局部修复；State/Commit/ViewState/Index |
| Predicates.h/.cpp | 几何谓词；精确数值依赖限 cpp |
| ProposalEvidence.h/.cpp | 提案内不可变样本事实；Samples |
| Certification.h/.cpp | 拟合、误差证据和接受条件；Samples/数值实现 |
| Proposals.h/.cpp | 接收目录与回收提案；Samples |
| Reservation.h/.cpp | 足迹、冲突与冻结贪心预留；Samples |
| Commit.h/.cpp | 拓扑准备与发布；State/Execution |
| Pipeline.h/.cpp | 自有状态/样本/网格的同步编排；Samples/Mesh |
| Mesh.h/.cpp | 实际拟合网格、脏范围及消费；Commit/公共地形网格类型 |
| Execution.h/.cpp | 确定性分块、局部账本归并；只借用同步 Dispatch，不持有线程池 |

`src/experiment/greedy_transactional_lod/` 保留 Input、ScalingProtocol、Validation、QualityReport、DynamicReference 五组文件。它们的命名空间仍属于 Experiment，头文件只导入签名需要的核心类型，cpp 显式使用核心命名空间。运行核心没有对这些研究设施或 DOD 的 include。

## 公共执行工具

**FACT**：`src/tools/CpuThreadPool.h/.cpp` 是原 DOD 线程池的唯一实现。名称归一化后，cpp 函数体与迁移前一致。旧 `DataOrientedRoamThreadPool.h` 提供别名，State/Pipeline 同步移除旧 class 前向声明；没有第二份调度器。

池持有互斥锁、任务队列、线程数组、两个条件变量、活动计数和停止状态。`EnsureWorkerCount` 只增长；`ParallelFor(1)` 内联，多个任务整批入队后唤醒，再等待队列与活动计数归零；`Shutdown` 让已提交任务处理完再 join。原始池仍要求任务异常不能越过线程入口；DOD 调用不新增异常向量。

`CpuTaskExecutor` 在工具层持有池、请求线程数和 `_failed`。`Dispatch` 按块创建异常槽，包装任务使异常留在对应槽；同步返回后按块顺序重抛。若入队过程抛出，则先置 `_failed`，在捕获栈内 Shutdown，再重抛，后续派发拒绝。异常槽分配失败发生在入队前，不停止实例。任务自身失败不停止实例。该组件不可重入，也不支持多个调用者同时派发。

MPR 的 `MaterializationExecutor` 现在只持有 `CpuTaskExecutor`，保留原 `Execution()` 返回类型；lambda 借用外层实例并转发 `Dispatch`。没有迁移 MPR 状态、目标或物化算法。

## 构建与消费者

顶层 `parallel_roam_cpu_execution` 为对象库，公共池和安全执行器只编译一次。原 DOD 源清单包含其对象；事务单独测试/探针直接链接该对象库，家族探针通过 DOD 清单消费，没有重复符号。消费者继续链接原 Threads 和 profiling 设置。

`cmake/TransactionalLod.cmake` 在 tests/app 之前定义核心及两个开关。`BUILD_TRANSACTIONAL_LOD` 默认关闭，启用时要求显式 Boost 1.90.0 和 GLM；`ENABLE_TRANSACTIONAL_LOD_RUNTIME` 默认关闭且要求核心。**PLANNED**：运行开关后的公共适配与注册由后续阶段加入，目前不会使应用出现第三算法。

核心严格浮点选项为私有，不改变 Classic/DOD。动态参考从核心移到 tests 内的研究静态库；事务测试和探针链接该库，家族探针无需动态参考。Input/Validation/QualityReport 仍只由研究入口编译。

`run_cpu_profile.py::source_manifest` 与 scaling 脚本通过 `git ls-files --cached --others --exclude-standard` 枚举现存源码，因此无需硬编码新清单，也不会把已删除旧路径当作当前实现。历史报告未改路径。

## 可验证边界

`CpuTaskExecutorTests.cpp` 在派发者线程注入一次分配失败，真实覆盖部分入队排空及实例停止；另测任务异常后复用、单线程、非法块数和旧别名。现有事务/MPR 夹具继续覆盖算法结果与错误传播。

核心可在关闭 tests/app 时构建。WSL 的关闭新功能配置只具备 bootstrap 应用依赖，不能据此声称完整图形应用通过；DOD 完整 CPU 路径由家族探针覆盖。公共适配、深度域、实际两后端资源和持续视图质量均未在本阶段实现或验证。
