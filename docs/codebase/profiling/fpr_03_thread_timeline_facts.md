# FPR-03 CPU 标记与会话事实

2026-09-14。对照当前源码，不将后续家族接入视为已实现。

`src/profiling/CpuProfiling.h` 提供项目宏到官方静态 RAII zones、Text、Value 和线程名的薄映射。关闭宏展开为空表达式且不求值参数；`CpuProfiling.cpp` 只在 Tracy 构建作为独立静态目标链接，负责连接代际和线程名，不持有算法或采集文件。

`ProfileSession` 根据 ROAM_PROFILE_BACKEND 选择 perf FIFO 或 Tracy。Tracy 构造等待最多 15 秒，Begin/End/Finish 检查连接代际；每轮 zone 在主线程持有到 End，元数据是 replay:round。Finish 的实际日志提交位于完成 zone 内，避免官方导出省略零时长空区间。会话仍只由工具/探针链接。

GTP 在原函数作用域标记初始化、视图、计划、认证、局部恢复及发布。Execution 的 dispatch 包含任务准备、同步派发和账本归并；每个 invoke 内生成真正同线程 task zone，单行文本标记 phase/chunk。没有每样本事件，没有替换 WorkLedger。

DOD ThreadPool 的原锁、队列、通知与领取顺序保持；增加入队、主线程等待、休眠、任务与完成作用域。Linux 名称用真实 gettid；非 Linux 使用通用名称且仍由 Tracy 区分实际线程。没有把任务索引当线程身份。

`tracy_backend` 从原能力工具提取共用采集生命周期，目标/采集独立进程组，180 秒与 512MiB 有界；只在捕获子进程设置 TRACY_NO_EXIT。官方 csvexport 分别输出 inclusive/self 展开，失败产物保留。

`tracy_report` 以 profile.frame 和 profile.complete 验证完整性；按同线程嵌套计算已标记 self，按时间及 phase/chunk 关联异步任务与派发。Tracy 导出 ID 不等同 OS TID，区间时长不是 CPU work。`run_cpu_profile` 支持同一 CLI 切换 perf/Tracy，版本验证要求 0.14.1，并保留实际工具身份。
