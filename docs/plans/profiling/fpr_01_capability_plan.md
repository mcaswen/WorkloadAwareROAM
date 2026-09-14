# FPR-01：工具能力与可选构建

2026-09-14；小规划。依据[大规划](cpu_function_profiling_major_plan.md)、[开发规范](../../standards/development_guidelines.md)、[规划规范](../plan_guideline.md)。用户已允许小规划自主实施；规划提交为 `77ef897`。

## 目标与边界

实际验证普通用户的 perf 软件事件、DWARF 调用栈和多线程采样，并以同版本 Tracy 客户端/capture/csvexport 记录、读回完整短轨迹。此阶段只建立工具能力和目标级构建，不修改算法、线程池和自然探针控制流；FPR-02 才接入业务采样窗口。

当前 Ubuntu 26.04 提供 `linux-perf=7.0.0-31.31`，但未安装。先下载并解包到用户缓存验证，缺少运行库时逐项记录；无须更换 WSL 内核或修改全局权限。Tracy 固定 v0.14.1，首次取用后先核对完整 Git 身份及归档哈希，再构建；工具源码和二进制不进入 Git。

## 文件职责

| 处理 | 文件 | 职责 |
| --- | --- | --- |
| Create | `cmake/Profiling.cmake`、`cmake/TracyVersion.cmake` | 目标级调试符号、唯一可选客户端、冻结来源及本地源码核验；不向默认目标传播依赖 |
| Extend | `cmake/ProjectOptions.cmake`、根 CMake、CMakePresets、tests/CMakeLists | 明确开关、独立目录和解析夹具目标；复用 Release 优化与项目选项 |
| Extend | `.gitignore` | 按现有分模块模式忽略 profiling 原始数据，不将 trace/二进制带入 Git |
| Create | `tests/CpuProfilingTests.cpp` | 两个已知热点、真实线程、等待与异常作用域；此夹具直接使用官方 CPU zones，公共封装在 FPR-03 再建立 |
| Create | `scripts/profiling/environment.py` | 只读环境能力与真实事件试验，输出命令/退出码/未知项；不安装软件或改权限 |
| Create | `scripts/profiling/capability.py` | 有界执行解析夹具、官方 perf/Tracy 导出及完整性检查；独立于后续自然 workload runner |
| Create | `tests/test_profiling_capability.py` | 失败、超时、缺失输出和导出证据的解析测试；不伪造自然结果 |

Tracy 来源优先显式本地目录，统一校验版本与源码身份。普通关闭构建不寻找客户端；调试符号单独开关，不能借 RelWithDebInfo 更改优化等级。夹具作用域 RAII 必须位于实际函数内，异常退出关闭区间，线程在退出前全部 join。只有夹具的已知热点允许 noinline，业务不使用该选项。

## 实施与验收

1. 保存现有普通构建的身份/相关配置及一组 test129 C4 八轮成本作为基线；同一存活 WSL 会话内顺序执行，预计几十秒以内，不与构建并行。
2. 在用户缓存准备 perf、Tracy 固定来源和 headless 工具，记录许可证与哈希。软件事件打开失败时保存原始错误，停止依赖该能力的后续阶段，不将纯 Tracy 替代宣称双工具可用。
3. 建立默认关闭、符号采样及 Tracy 开启构建，夹具记录确定结果、总耗时和线程身份。符号构建用 `cpu-clock:u`、499Hz、DWARF 栈；最多一次约一秒的已知计算夹具，不把它用于算法性能结论。
4. Tracy 等待连接有 15 秒上限，采集进程有总超时；区间必须读回两个热点、至少两个实际线程、等待和异常退出，且有最终完成区间。捕获工具与目标顺序结束并排空，禁止无限等待。
5. 默认关闭夹具及相关既有测试，校验关闭目标没有 Tracy 符号和线程；同条件复测 test129 C4 八轮，按开发规范 `max(0.05ms,5%,已有波动)` 筛查，仅具体风险允许一次复测。符号/客户端新增成本单列，不视为普通算法回归。
6. 写代码事实、能力报告与审查，更新大规划阶段结果后进入 FPR-02。

不跑完整 CTest、图形后端、多场景线程矩阵，不以 cycles/cache/sched 可用作为初版硬门槛。原始日志和工具清单进入 `benchmark-output/profiling/fpr-01/` 的独立运行目录；不覆盖失败记录，不进入 Git。

## 实现情况

实施前规划已完成；正在进行能力准备。后续结果必须分别登记 perf 事件/栈/线程与 Tracy 连接/落盘/读回，不能仅凭工具版本输出判定通过。

实现中已确认的环境调整：perf 7.0.14 用户态软件及硬件计数均可实际打开。DWARF 的两个子线程展开报 `No such process`，帧指针独立构建则三个线程都能恢复已知两层调用；依大规划条件变体选用 FP 继续，保留 DWARF 原始失败，不宣称其已修复。直接写 `/mnt/d` 时遇到 `Bad address`，同一工具写 ext4 可用，因此原始采集改用用户 Linux 缓存，完成后复制归档到 benchmark-output；不改算法边界。

Tracy 官方发行提供 Linux headless 二进制，采用同一 v0.14.1 的 capture/csvexport，登记发行归档和实际工具哈希。这样无需构建查看器及其额外依赖；客户端仍由本工程编译并核验 99 个客户端/构建/许可文件的汇总身份。

阶段已闭环：FP 187 个样本覆盖三个计算线程及已知父子调用；Tracy 读回 12 个预期区间和完整尾部。两个夹具、三个脚本用例、错误源码配置拒绝均有记录。普通原型二进制 SHA 不变，test129 C4 八轮前后决策相同，均值 18.671310→17.115964ms；同二进制波动不解释为优化收益。详见[报告](../../research/profiling/fpr_01_capability_report.md)、[事实](../../codebase/profiling/fpr_01_capability_facts.md)和[审查](../../reviews/profiling/fpr_01_capability_review.md)。后续自然采样采用已明确编译差异的 FP 变体。
