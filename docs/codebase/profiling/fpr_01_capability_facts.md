# CPU profiler 能力与构建代码事实

2026-09-14；范围为 FPR-01 新增构建、解析夹具和工具验证。以下为当前代码事实；尚未接入业务 zones 和自然采样窗口。

## 文件、依赖与状态

- `cmake/ProjectOptions.cmake` 定义五个开关/来源项，默认 profiling/符号/帧指针/夹具均关闭
- `cmake/TracyVersion.cmake` 保存版本、完整提交、源码归档、Linux 工具归档及客户端树身份
- `cmake/Profiling.cmake` 生成 `parallel_roam_profiling_options` 接口目标，根项目选项目标传播到实际核心和入口编译单元；不使用全局编译 flags
- 关闭时函数提前返回，没有查找、下载和链接 Tracy；开启时显式本地源码或已许可 FetchContent → 实际内容核验 → 唯一 `TracyClient` → 统一 CPU 特性
- 客户端树哈希覆盖排序后的 99 个 public 文件、CMake 脚本及 LICENSE，序列化为 `relativePath=sha256\n`。它验证客户端构建输入，不宣称校验发行查看器；命令行工具使用另一个官方发行归档哈希
- 三个 Linux presets 分别保留 DWARF 原配置、条件性 FP 配置和 Tracy 配置，使用各自 Linux 用户缓存构建目录。默认 Release 的优化与浮点规则不变
- `tests/CpuProfilingTests.cpp` 直接用官方 zones，只在夹具定义 `noinline`；`volatile` 栈种子让 FP 也可观察叶函数的调用者。输入控制已知整数计算次数，不是自然性能负载
- `scripts/profiling/environment.py` 记录环境并有条件独立试开事件。`command` 返回状态/退出码/标准输出/错误/墙钟，失败和缺失不抛成成功
- `scripts/profiling/capability.py` 是独立解析工具验证入口，依赖 environment/标准库/外部工具，不依赖 LOD。自然 runner 尚未实现

## 调用与生命周期

`CMake → parallel_roam_prepare_profiling → 可选源码核验 → TracyClient`。符号与客户端开关独立；`-g` 不改变 Release 的 O3。FP 单独增加 `-fno-omit-frame-pointer`；无法保证未重新编译的系统库所有帧可恢复。

夹具在 `ROAM_PROFILE_WAIT` 存在时等待连接，最多 15 秒；关闭构建在请求连接时失败。主线程及两个 `jthread` 各调用一次 Parent/Leaf，独占结果槽，join 后读取。异常区间通过正常栈展开关闭；`fixture.complete` 在完整帧结束后生成。JSON 输出位于帧外。

`capability perf → perf record → perf script/report → 逐 TID 栈覆盖`。缺栈的样本保留分母，三个 TID 都必须出现已知 Parent/Leaf 才通过；它是特定夹具断言，不能用于假定自然路径只有三个线程。

`capability tracy → capture + target → 等待正常退出 → csvexport -u → 数量/线程/结束顺序核验`。采集/目标分别是新进程组；异常或超时在 finally 定向终止自有进程组。客户端只为该次采集设置 `TRACY_NO_EXIT=1`，没有全局永久等待选项。输出独立文件，避免目标阻塞在没人读取的 stdout 管道。

原始采集目录在 Linux 用户缓存；完成后 copytree 到排他创建的 benchmark-output 归档目录。失败也保留。未自行删除 native 目录，结果登记其路径及 SHA。Windows 挂载盘不承担 perf 原始写入。

## 验证与成本

见[能力报告](../../research/profiling/fpr_01_capability_report.md)。当前软件和硬件事件可试开；FP 可恢复三个计算线程。DWARF 在此次 perf/libdw/WSL 组合下只恢复主线程，原因没有证明，不推广为 DWARF 一般不可用。

Tracy 正常捕获和官方展开导出已实跑，三条业务线程、12 个预期区间和尾部完成标志一致。CSV `thread` 是导出的线程标识，不直接冒称 OS TID；未来跨工具关联使用逻辑任务身份。

默认构建重新生成后现有原型没有重编译，二进制 SHA 未变，八轮决策一致。夹具的等待、连接和客户端启动是验证成本，不作为真实算法性能。

## 符号与修改风险

- `parallel_roam_verify_tracy_source`：修改冻结版本必须连同哈希、许可和工具协议重新核验
- `parallel_roam_prepare_profiling`：所有已启用编译单元必须通过同一接口目标；不得重复定义客户端
- `ProfileHotLeaf/ProfileHotParent`：仅用于栈能力验证；不能复制 noinline/volatile 手法到业务热点
- `WaitForCapture/ProfileException/main`：连接边界、异常结束及线程排空
- `command/inspect_environment`：工具错误与实际事件结果
- `perf_stack_coverage/tracy_fixture_coverage`：缺栈分母、区间尾部和多线程覆盖
- `capture_tracy/stop_process`：自有进程组生命周期；不能对外部正在运行的采集器发结束信号

## 未确认事项

DWARF 子线程 `No such process` 与 `/mnt/d` 直接写入 `Bad address` 的底层原因未确定。尚未验证自然 workload 的样本充分性、完整采集窗口、公共标记关闭语义与大规模 trace 扰动，分别属于后续阶段；没有 GUI/GPU 或系统调度追踪。
