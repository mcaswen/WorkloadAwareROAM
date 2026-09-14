# FPR-03：函数、任务和等待时间线

2026-09-14。承接 `fa8bfac`、[大规划](cpu_function_profiling_major_plan.md)和[实际函数证据](../../research/profiling/fpr_02_function_sampling_report.md)。用户已批准后续小规划自主闭环。

## 范围与文件职责

- Create `src/profiling/CpuProfiling.h/.cpp`：统一静态作用域、低频元数据、线程命名与连接身份；宏关闭时不求值参数，cpp 仅启用 Tracy 时链接。算法不依赖采集会话。
- Extend `cmake/Profiling.cmake`：已冻结客户端之上增加薄封装目标，不影响原优化/浮点标志。
- Extend `ProfileSession`：复用窗口与完整性状态，按显式环境选择 perf 或 Tracy；Tracy 首轮等待最多 15 秒，以连接代际检测断开/重连，每轮 profile.frame 与最终 profile.complete 在同线程完整结束。
- Extend GTP Pipeline、Samples、Proposals、Certification、Reservation、Execution、Commit、Mesh：静态函数/阶段标记，依据 perf 选择 Receivers/Donor/Fit/Measure/Accepts 与批级视图、样本恢复、建序、发布。每 Q 的 Project/Weights/ErrorBounds 不标记。
- Extend DOD ThreadPool：既有入队、任务、主线程等待、休眠与完成范围，不换锁、不改变任务或调度；线程名使用真实线程身份。Execution 每个任务附 phase/chunk 元数据，线程任务关联到其所在 frame；不伪造跨线程调用栈。
- Extract `scripts/profiling/tracy_backend.py`：从能力采集复用启动/清理逻辑，补完整 argv、期限、官方导出；`run_cpu_profile.py` 扩展后端，不复制算法 runner。
- Create `scripts/profiling/tracy_report.py`：官方展开区间的 ROI、嵌套、任务与等待聚合；与 perf 格式不同，独立文件避免报告模块混杂。
- Extend 解析夹具与定向 Python 测试：关闭参数无求值、完整尾标、连接失效、坏字段；共用池异常由现有 GTP 并发测试覆盖。

## 时序和失败契约

`profile.frame` 是原 frame_update 的同线程外包区间；`profile.complete` 必须在最后业务区间之后。导出线程 ID 只称 Tracy 线程标识，不当作 OS TID。GTP 调用者阶段与异步任务分别表示，任务元数据一段短文本；任务区间在实际执行线程构造/析构。等待包含抢占与系统等待，没有 sched 数据不推导具体阻塞根因。

先连接后运行初始化，报告仅选择 ROI 内区间。采集和无连接运行分别执行；采集子进程才设置 TRACY_NO_EXIT，180 秒总期限、10 秒排空与 512MiB 文件上限。断连、数量缺失或未导出结束标记均保留不完整证据，不以文件存在判成功。

## 验证与停止

两场景各一个进程、原八轮 C4；不重复 perf 补采、不增加 workload。比较无连接与捕获的 mesh 和非时间摘要，记录每轮标记数量/包围关系、实际线程、等待和任务尾差。基础函数标记越过建议 10% 扰动线时仅缩小已标记细节，必要时保留结构诊断用途，不修改算法。

关闭构建继续与 FPR-02 前旧备份比较，保留此前 +11% 的疑点；新插桩应预处理移除且不链接 Tracy。核心对象变化须核对是否仅行号/标记代码，不能把不同编译设置的差异当 profiler 成本。FPR-02 的原始样本和正确性比较可复用。

本阶段不补整个 Classic/DOD 矩阵；FPR-04 接家族窗口与使用说明。完成后将实际时序、关闭/无连接/捕获成本和未解决项写入报告、事实、审查，再提交。

## 实现情况

已完成两场景八轮完整捕获、16 帧结果比对及实际断连/异常验证，见[报告](../../research/profiling/fpr_03_thread_timeline_report.md)与[审查](../../reviews/profiling/fpr_03_thread_timeline_review.md)。关闭路径疑点未持续复现；Peking 捕获单轮 P95 扰动保留结构诊断限制。没有改变算法或扩张测试矩阵。
