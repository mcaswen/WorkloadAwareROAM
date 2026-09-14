# TPI-01：运行核心与共享执行依赖

> 2026-09-15，小规划。依据已批准的[接入大规划](transactional_platform_integration_plan.md) §3/4/10；用户授权按阶段自主实施与提交。状态：实现与定向核查完成；性能归因边界见下文。

## 目标与边界

使事务化运行核心在不构建研究测试时也能独立链接，并消除执行器对 DOD 私有目录和 MPR 实验层的反向依赖。本阶段只迁移职责、类型命名和构建归属，不改变候选、证书、预留、分块或发布算法。公共适配、深度规则和图形接入留给后续阶段。

已读开发、规划、审查规范及[接入边界事实](../../codebase/cpu_refinement/transactional_platform_integration_baseline.md)。源码核查覆盖两个 CMake 入口、核心/研究头文件、DOD 池与前向声明、MPR 安全适配和相关测试/归档脚本。当前核心构建藏在 tests 中且包含动态研究参考；MPR 包装器已经具有任务异常收集和部分入队失败排空规则，直接提取这份规则。

## 文件与职责

| 判断 | 文件 | 本阶段职责 |
|---|---|---|
| Move / Reuse | 大规划 §4.1 所列 26 个核心文件，移至 `src/algorithms/greedy_transactional_lod/` | 保留计算实现；命名空间与运行算法层一致 |
| Extend | 保留在实验目录的 Input、ScalingProtocol、Validation、QualityReport、DynamicReference | 显式使用核心类型，研究输入/评价不进入核心库 |
| Move | `src/tools/CpuThreadPool.h/.cpp` | 原 DOD 池的唯一实现，任务调度行为不变 |
| Wrap | 旧 `DataOrientedRoamThreadPool.h`，DOD State/Pipeline 前向声明 | 兼容类型别名；删除旧池 cpp，避免双实现 |
| Extract | `src/tools/CpuTaskExecutor.h/.cpp` | 持有池、线程数和失败状态；同步 `Dispatch` 收集异常并排空 |
| Wrap | `MaterializationExecutor.h/.cpp` | 保留 MPR 接口和回调生命周期，委托公共执行器 |
| Create | `cmake/TransactionalLod.cmake` | 项目级核心开关、固定 Boost、严格浮点和核心清单 |
| Extend | 顶层及 tests CMake | 公共执行库只编译一次；所有旧消费者链接同一实现；动态参考只链接研究目标 |
| Extend | 现有事务/MPR 测试、当前来源归档脚本 | 更新实际路径，保留历史报告原样；增加共享执行器的定向错误边界案例 |

依赖：研究入口 → 核心；DOD/MPR/未来适配 → 公共执行工具；公共执行工具只依赖标准库和已有 profiling 宏。核心不依赖研究、DOD、窗口或渲染器。研究文件以显式核心命名空间/类型导入使用迁移后的类型，不在算法层提供实验命名空间兼容。

`CpuTaskExecutor::Dispatch(chunks, task)` 返回前所有任务结束；任务异常在排空后传播，允许下次调用；入队异常排空并永久停止该实例。保持现有 0/超线程数拒绝与单线程行为。不会把线程池的原始 `ParallelFor` 改为带新异常开销的 DOD 热路径。

## 实施顺序

1. 提交已批准大规划，保存 `c7e6cd1` 对应基线程序、源码哈希和构建缓存；在当前环境采一次修改前对照。
2. 移动核心和共享池、提取同步执行器；更新有限调用方与清单，不机械改写历史文档。
3. 核心选项移出 tests，默认关闭的运行开关必须依赖核心；无 tests 配置能构建核心，关闭核心的旧应用依赖不增加 Boost。
4. 运行下述定向验证，记录结果、复杂度未变的依据、架构审查，提交后进入 TPI-02。

## 验证与性能契约

- 主要构建复用 WSL Release `/home/mcaswen/workload-roam-nmp01p-build`，Boost 路径沿用缓存中的固定 1.90.0。不同时采样和构建。
- 正确性选 `transactional_lod`、`transactional_priority_index`、MPR 的无自然参数夹具和公共执行器定向测试。覆盖串并行结果、任务异常后复用、部分入队失败排空与拒绝再调用，不重跑 MPR 自然矩阵。
- 同一 SVE 冻结 Peking/20k/sample14、r=m=64，复用原八轮协议。B1 固定 CPU 0，C4 和 DOD4 固定 0,2,4,6。每配置前后各一独立进程；三项顺序运行，无额外预热。来源是 `sve-01/run-01/freeze/b20000/peking547-sve-orbit64-b20000-14.json`，不是本规划后续的新种子。
- 命令保留原 probe 参数：B1 `trajectory-b-timing --limit-policy fixed64`；C4 `trajectory-c-timing --limit-policy fixed64 --workers 4`；家族入口 `dod --workers 4`。完整 argv、程序/输入身份、日志记录在 `benchmark-output/cpu-refinement/tpi-01/run-01/`。比对八轮逻辑字段、输出几何身份以及整体/受影响阶段时间。
- 预计采集远低于 2 分钟；单进程 180 秒，阶段采集上限 10 分钟。明显退化按 `max(0.05ms,5%×基线,已知波动)` 筛查，最多对相关配置再跑一次前后对照。短序列分位数只作工程描述，不充当统计重复。
- 配置验证额外覆盖 `BUILD_TESTS=OFF` 核心构建及全部新特性关闭的旧目标；不运行图形后端或 SVE 全矩阵。检查核心源码无实验 include，链接清单无重复池、无研究参考。

## 风险与出口

重点风险是旧前向声明与类型别名冲突、研究辅助函数 ADL/命名空间解析、静态库链接漏项、源码归档遗漏新路径。逐项检查，不通过复制核心或重新引入实验依赖绕过。

通过本阶段只表示迁移/调度兼容与核心构建独立，不表示平台接入、持续质量或性能竞争力通过。任何实际性能退化单独记录分析；仅在已批准迁移边界内修复。

## 实现结果

迁移、定向构建与四项夹具已完成。首次计时及唯一相邻复测中，B1/DOD4 的变慢不持续，C4 移动帧仍有约 19% 差值，集中在视图评分；函数体与逻辑工作量归一化核查未变。对此新增一对限定 C4 的 `perf stat` 调度诊断，使用保留程序，不增加场景或修改算法；其含初始化/导出的进程计数不冒充阶段 CPU 时间。原始复测保留，不以诊断挑选快样本替代。

迁移结果见[实现事实](../../codebase/cpu_refinement/tpi_01_runtime_core_facts.md)及[实施审查](../../reviews/cpu_refinement/tpi_01_runtime_core_review.md)。四项定向夹具通过；26 个核心文件和池函数体逆变换后与基线相同；自然逐帧逻辑与输出一致。无 tests 核心构建成立，关闭特性的 WSL 应用只验证 bootstrap 构建，完整图形路径留待后续。性能完整记录见[差值核查](../../reviews/cpu_refinement/tpi_01_performance_audit.md)，未把短测不确定性写成正式性能等价。源码归档脚本已经枚举现存 src，无需修改硬编码清单。
