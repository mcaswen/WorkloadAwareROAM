# FPR-04 公共入口与归因代码事实

2026-09-14；承接[窗口事实](fpr_02_function_sampling_facts.md)与[Tracy 事实](fpr_03_thread_timeline_facts.md)。

## 探针与算法边界

`GreedyTransactionalLodFamilyProbe` 额外接受 `--profile 1`，会话与 GTP 共用。原十四帧引导不调用 Begin/End；step14～21 环绕 BuildRenderData，结束后才进行公共包验证、JSON 和最终全 Q 离线评价。普通入口、设置与引导数量不变；首版家族不支持多次重放。

DOD Pipeline 的 BuildInternal 与 PrepareDataOrientedRoamFrame 有静态 zone；ExecuteDataOrientedRoamPass 的五个原 switch 分支分别标记评分、拓扑与网格。Classic 适配入口、MeshBuilder::Build、原 dual queue update 与 mesh 调用作用域有标记；没有把 Classic 拆成另一控制器，也未改变内部队列。

## 工具入口

`run_cpu_profile.py` 接受 GTP 三种轨迹计时或 classic/dod；家族重放超过 1 在访问工具前拒绝。后端仍只负责外部采集，执行控制留在已有探针。采集后核对实际程序哈希未变化，避免以变更中的程序确认符号身份。

`archive_inputs` 只读取真实依赖：snapshot、同目录 source JSON、场景/相机 CSV；家族另取 CSV 对应的高度资产。不遍历全部资产。复制的输入及其路径/哈希进入本次 manifest，失败仍归档；早期数据的补归档单列为事后工作。

`tracy_report` 可列家族构建与五阶段，同时单列 ROI 的池线程数量。GTP phase/chunk 派发分析只对存在这些标签的路径执行，Classic 没有线程任务不会被错误判定为失败；没有入口标记显示“未标记该入口”，不以零代替未知。

## 证据和使用

[最终归因](../../research/profiling/cpu_function_profiling_findings.md)引用两次有效 perf、两次 GTP Tracy 和一个 DOD Tracy，未重新扩大性能矩阵。`perf annotate` 的 Project 554 个样本与 addr2line 源位置实际验证，使用已存二进制和哈希匹配的旧源码。

[使用契约](../../research/profiling/cpu_profiling_usage.md)描述当前版本、WSL、独立构建、两后端、家族入口、关闭和失效边界。它不安装新内核、不改全局权限、不推导硬件带宽或真实调度原因。
