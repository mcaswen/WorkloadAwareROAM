# CPU 函数热点与线程时序分析大规划：perf + Tracy

> 2026-09-14；Major Plan，阶段编号 FPR。**用户已确认实施，并允许后续小规划自主闭环；先提交现有规划，再依次实施。** 本规划是后续工作削减的测量前置，不启动 GWR 优化。
>
> 依据：[开发规范](../../standards/development_guidelines.md)、[规划规范](../plan_guideline.md)、[GTP-04 代码事实](../../codebase/cpu_refinement/gtp_04_multicore_facts.md)、[性能与复杂度分析](../../research/cpu_refinement/gtp_platform_performance_analysis.md)、[GWR 待确认规划](../cpu_refinement/greedy_transactional_work_reduction_plan.md)。当前提交基点为 `b13aaf5`；此前未提交的 GWR 文档继续保留。

## 1. 问题、目标与完成标准

目前 `WorkLedger::Seconds` 和既有阶段统计能够指出“接收认证贵”“视图刷新贵”，尚不足以区分函数自身计算、下层调用、容器分配、精确算术和线程等待。源码指出的冗余只能作为假设，不能直接决定下一项优化。

本规划建立可重复使用的函数热点采样与线程时序工具链，必须实际用于当前原型。交付不能停在“链接了 Tracy”或“安装了 perf”，需要同时得到：

1. 优化构建上的函数自身/含下层调用热点、调用路径和源位置，覆盖没有手工标记的标准库、分配器和精确算术。
2. 同一算法任务中，主线程与实际执行线程的阶段、函数区间、任务开始/结束、等待与发布时序。
3. 正确区分运行时、初建、输入输出和独立诊断；能够限定采集窗口，避免百万样本初始化淹没更新热点。
4. 关闭、启用未连接、实际采集的成本证据；普通发布构建不链接 Tracy、不启动采集线程、不执行标记参数。
5. 对当前候选优化给出“证据支持/证据不足/优先级降低”的函数级清单，再讨论 GWR 的实施顺序。

本轮不优化算法，不改变 priority、Q、目录、预算或认证。不承诺找到的最大函数一定可优化，不把 profile 时间直接作为正式论文性能数据，也不以采样替代正确性验证。

## 2. 两种工具的分工

| 能力 | 主工具 | 数据语义与限制 |
| --- | --- | --- |
| 全路径函数热点、调用栈、源码/汇编定位 | perf record/report/annotate | 基于事件采样，不是每次调用计时；内联、尾调用、缺失符号和展开失败须报告 |
| 用户代码阶段、选定函数、任务与等待区间 | Tracy CPU zones、线程名、有限元数据 | 插桩区间墙钟包含抢占和等待；仅有标记的函数才有调用数量，不能覆盖所有 C++ 函数 |
| 进程 CPU 与少量计数 | 既有进程 CPU 采样、条件性 perf stat | 进程时间包含所有线程；硬件事件是否可用由能力实测决定 |
| 算法实际操作与结果 | 现有 WorkLedger、冻结输入及独立 oracle | 记录工作与正确性，不能从样本占比反推操作数量 |
| 真实完整速度 | 无采集的普通优化构建 | 不用 Tracy 区间总和或 perf 采样比例代替完整墙钟 |

默认分两次运行 perf 与 Tracy，使用同一源码、输入与任务定义，通过场景/轮次/阶段身份关联。两次执行的线程调度和时钟不同，不能将它们拼成同一条精确时间线。初版不同时开启两套采样器。

perf 提供用户态调用栈的 DWARF/帧指针选择；Tracy 采用原生作用域标记而非重写计时器。[perf record 文档](https://man7.org/linux/man-pages/man1/perf-record.1.html)、[Tracy 客户端接口](https://github.com/wolfpld/tracy/blob/v0.14.1/public/tracy/Tracy.hpp)

## 3. 当前事实与环境能力边界

### 3.1 已有工程能力

- `src/tools/PerformanceTimer.h/.cpp`：已有墙钟/累加计时，不替换其历史口径。
- `src/algorithms/TerrainLodProfiling.h`：Linux 使用 `getrusage`，Windows 使用 `GetProcessTimes`，已能读进程 CPU 时间；并非函数 profiler。
- `TransactionalExecution::Run`：同步派发、分块、局部账本和归并，已有实际线程数量诊断，但没有时间线。
- `DataOrientedRoamThreadPool`：共用任务池，具有入队、唤醒、取任务、执行及完成等待边界；任务索引不等于物理线程身份。
- 原型及家族探针已有冻结输入、八轮轨迹、诊断/计时分离和拒绝覆盖输出能力。
- CMake 已有可选依赖、实验目标和 Release 配置；没有 Tracy、perf 控制或函数采样接入。Windows 环境脚本不能直接承担 WSL 能力检测。

### 3.2 本轮只读环境核查

| 项目 | 观测 | 不可外推的内容 |
| --- | --- | --- |
| 系统 | Ubuntu / WSL2，`6.18.33.2-microsoft-standard-WSL2` | 不代表原生 Linux 或 Windows 性能 |
| 命令 | PATH 中未找到 perf、tracy-capture | 未安装命令不等于内核不能采样；未实际采集 |
| 内核配置 | `CONFIG_PERF_EVENTS=y`、`CONFIG_STACKTRACE=y`、`CONFIG_FTRACE=y` | 不保证每种事件均可打开 |
| 权限 | `perf_event_paranoid=2`、`kptr_restrict=1` | 用户态事件应实际试开；不能仅凭数字宣称可用 |
| 事件源 | sysfs 列出 software、cpu、tracepoint 等 | cpu 设备存在不证明 cycles/instructions 在虚拟机里有效 |
| 调度追踪 | 当前用户不可读 `/sys/kernel/tracing` | 本轮不能承诺 sched_switch/off-CPU 分析 |

FPR-01 将“命令存在、事件可打开、非零有效计数、子线程覆盖、调用栈可解析”分别验证。硬件 PMU 不可用时以用户态软件 CPU 时钟采样继续；软件采样也失败时不得把纯 Tracy 接入标为双工具闭环完成。

默认只采当前实验进程，不使用全系统 `-a`。不为完成 profiler 自动修改内核、`.wslconfig`、永久 sysctl、全局权限或安全策略。现有权限无法满足最低能力时，先记录实际错误和所需环境变更，再讨论；不能静默改成系统全域采集。[Linux perf 权限文档](https://www.kernel.org/doc/html/latest/admin-guide/perf-security.html)

## 4. 范围、接入对象与复用

初次真实使用以持续事务原型为主：GTP 的 B/C，优先四线程两自然场景。Classic/DOD 的现有家族探针接入同样的采集窗口和少量阶段标记，保持旧实现可测；不展开家族完整 profiling 矩阵。

本轮实现公共 CPU 标记能力，但不接 GUI 控件、图形资源、GPU zones、分配器替换、全局 new/delete 拦截、锁类型替换或平台上传。Tracy 图形查看器可离线打开产物，程序化验收依赖命令行采集/导出，不要求用户手动操作窗口。

| 判断 | 能力 | 处理 |
| --- | --- | --- |
| Reuse | PerformanceTimer、WorkLedger、进程 CPU 读数 | 保留现有统计；过程 CPU 读数仅作低频辅助，不塞入每函数入口 |
| Reuse / Extend | GTP/家族探针、冻结来源与线程池 | 添加明确采集窗口、少量标记；不复制另一份算法 runner |
| Wrap | Tracy 客户端 | 一个薄的项目宏/元数据边界，统一关闭语义与依赖 |
| Wrap | perf 和 Tracy 官方 CLI | Python 管进程、参数、证据和输出，不自造二进制 trace 格式 |
| Create | 采集会话与工具后端 | 协调 ROI、连接和退出，归工具层，不放入算法控制器 |
| Reuse / Extend | `scripts/cpu_pilot_support.py` 的纯文件身份函数 | 只复用哈希/元数据能力；不调用全仓库/下载目录遍历作为热路径，不引入 Windows 环境依赖 |

## 5. 依赖与构建决策

### 5.1 Tracy 版本与来源

建议冻结官方 `v0.14.1`，客户端、capture、csvexport 及离线查看器使用相同版本。该发行版与协议/按需连接行为相关；不使用 master 或未经验证的系统随机版本。[官方发行页](https://github.com/wolfpld/tracy/releases/tag/v0.14.1)

FPR-01 在首次构建前登记完整提交 SHA、源码归档 SHA256 和许可；当前没有下载或安装。依赖放到显式的工具缓存/`PARALLEL_ROAM_TRACY_SOURCE_DIR`，项目只提交锁定信息及构建接入，默认不开网络获取。需要自动取依赖时遵循现有显式 FetchContent 开关，且使用冻结地址与校验值。保留官方 BSD-3-Clause 许可，不手抄客户端内部实现。[Tracy 许可](https://github.com/wolfpld/tracy/blob/v0.14.1/LICENSE)

perf 优先使用 Ubuntu 可用的用户态工具并验证功能；不能假定有对应自定义 WSL 内核名的 `linux-tools-$(uname -r)` 包。包不适用时，最多采用一个冻结的官方 Linux/WSL 源码版本构建 tools/perf，安装在用户工具目录；不编译或切换内核。版本不完全相同不自动判失败，实际事件和解析验证决定能否使用。[内核文档中的独立 perf 构建说明](https://www.kernel.org/doc/html/latest/admin-guide/workload-tracing.html)

### 5.2 三类构建与一个条件变体

| 构建 | 配置 | 用途 |
| --- | --- | --- |
| 普通 Release | 现有优化和浮点选项；所有 profiling 标记编译移除 | 正式计时及关闭开销参考 |
| perf Release | 保持 Release 优化，增加调试信息；Tracy 关闭；默认 DWARF 展开 | 函数热点采样，不以默认 O2 的 RelWithDebInfo 替代原 O3 |
| Tracy Release | 同级优化/调试信息，显式链接一个 TracyClient | 阶段/函数/线程时序和插桩开销 |
| 条件性 FP 诊断构建 | 仅 DWARF 展开效果差或记录开销超限时，保留帧指针 | 明确编译差异和成本；不无声替换 perf 主构建 |

不全局 `-fno-inline`、不使用 `-finstrument-functions`，不为了看到函数而改变其调用频率。内联函数按 DWARF/指令位置归因，不能展开的部分如实保留。帧指针和尾调用相关开关可能改变代码生成，必须与正常构建分开记录。

新增目标级设置，建议为 `PARALLEL_ROAM_ENABLE_TRACY`、`PARALLEL_ROAM_PROFILE_SYMBOLS`、`PARALLEL_ROAM_PROFILE_FRAME_POINTERS` 及受限的 Tracy 细节级别；默认全部关闭/基础级。配置应用到被采样的实际 core/object 源码，不只给外层可执行文件加 `-g`。每个进程只能有一个 TracyClient，所有相关编译单元使用一致特性宏，不混用开/关对象缓存。

Tracy 初版固定 CPU zones，启用按需连接与仅本机连接，关闭内置采样、系统追踪、帧图像和全局 callstack。`TRACY_ENABLE`、`TRACY_ON_DEMAND`、`TRACY_NO_SYSTEM_TRACING`、`TRACY_NO_SAMPLING` 等由统一 CMake 目标传播，不散落业务文件；不默认启用 `TRACY_NO_EXIT` 造成无连接永久等待。工具采集需检查完整排空，见 §8。[官方 CMake 选项](https://github.com/wolfpld/tracy/blob/v0.14.1/CMakeLists.txt)

## 6. 目录、文件与职责设计

公共标记归 `src/profiling/`；进程/ROI 握手归 `src/tools/profiling/`；外部工具编排归 `scripts/profiling/`。此能力适用于多个算法，不放入 GTP 或 DOD 内部作为共享工具。

```text
src/profiling/
  CpuProfiling.h
  CpuProfiling.cpp
src/tools/profiling/
  ProfileSession.h
  ProfileSession.cpp
cmake/
  Profiling.cmake
scripts/profiling/
  environment.py
  perf_backend.py
  tracy_backend.py
  report.py
scripts/run_cpu_profile.py
docs/{plans,codebase,reviews,research}/profiling/
benchmark-output/profiling/fpr-xx/       # 忽略的原始产物
```

| 动作 | 文件 | 主要职责和独立理由 |
| --- | --- | --- |
| Create | `CpuProfiling.h` | 作用域函数/阶段标记、低频注释和编译关闭宏；必须在调用者真实作用域持有 RAII zone，不能在 helper 内创建后立即析构 |
| Create | `CpuProfiling.cpp` | 已启用客户端的线程命名、程序元数据和连接查询等薄封装；不拥有算法、计时报告或采集文件 |
| Create | `ProfileSession.h/.cpp` | 仅探针链接的会话、ROI 控制/应答、连接门禁、窗口标签及有界退出；POSIX 控制不进入核心算法 |
| Create | `cmake/Profiling.cmake` | 版本锁定核验、唯一客户端目标、目标级符号/插桩设置；不继续扩张通用依赖文件 |
| Extend | ProjectOptions、根 CMake、CMakePresets、tests/CMakeLists | 显式选项/独立构建目录及受影响目标；默认不查找/下载/链接 Tracy |
| Create | `environment.py` | WSL、事件、权限、符号与工具能力清单；结果区分未知/失败/可用，不改系统设置 |
| Create | `perf_backend.py` | perf 命令、控制通道、事件与展开配置、样本丢失/符号报告；不理解 LOD 决策 |
| Create | `tracy_backend.py` | 本机 capture 启动、连接/完成检查、官方 csvexport、trace 版本；不自解析 .tracy |
| Create | `report.py` | 归一化函数/调用路径与区间数据、质量标签及中文报告；不生成采样数据或选择更快输入 |
| Create | `scripts/run_cpu_profile.py` | 用户入口，校验输入、选择一个采集后端、统一生命周期和独立输出目录 |
| Extend | 两个现有 GTP 探针 | 保留旧 CLI 行为，添加显式 profile 模式/可选会话参数，ROI 环绕原执行；同一循环复用，不能复制控制逻辑 |
| Extend | GTP Samples/Proposals/Certification/Reservation/Execution/Commit/Mesh/Pipeline | 各自在自身职责内放置选定的 CPU zones；高频内层先靠 perf，不批量给所有函数插桩 |
| Extend | DOD Pipeline/PassExecution/ThreadPool，Classic Builder/适配入口 | 少量已有阶段及线程池等待/执行标记；不改队列、调度、锁类型或算法设置 |
| Create / Extend | `tests/CpuProfilingTests.cpp`、脚本相关测试、现有原型测试 | 关闭语义、会话失败、真实线程、工具解析及算法不变；不建设新通用测试框架 |

细函数标记清单由 FPR-02 的 perf 证据决定，在 FPR-03 小规划冻结；不能以这条规则无边界扫描/修改所有源码。第三方 Boost/STL 不插入项目标记，使用调用栈和必要的调试符号定位。

依赖为 `算法 → CpuProfiling → TracyClient(仅显式启用)`，`探针 → ProfileSession → CpuProfiling/系统控制通道`，`Python runner → perf/tracy backend → 官方工具`，报告只依赖导出数据。公共 profiling 不依赖 GTP、DOD、GUI 或 renderer；正式关闭目标不依赖会话工具。

## 7. 函数/阶段标记及线程时序契约

### 7.1 粒度

基础级保留完整 `frame_update`、SetView、Update、Prepare/Publish、输出维护、派发/等待/归并及线程任务区间。函数级首选每个 root/提案调用一次的 Receivers、Donor、Fit、Measure、Accepts 等入口；函数名、文件和行号由静态源位置提供。

Samples 的 PrepareView、BuildOrders、PublishView、局部 Prepare 分开；Reservation 的接收准备、回收准备、pair/reservation 分开。这份候选表用于覆盖流水线，不预断最大函数。ClipPolygon、CoveringFace、Weights、精确算术和 allocator 先用 perf，只有证据表明需要精细时序才在单独细节级开放。

禁止对每个 Q、字段读取、容器比较或每次精确乘法发事件。标记和逻辑计数互补；关键内部循环可用一段作用域加一次累计数量，不为精确调用计数制造百万事件。

### 7.2 线程、批次与等待

- 主线程记录完整派发区间、入队/通知、完成等待和账本归并；线程任务区间在真正执行任务的线程内开始/结束。
- 批次/阶段/chunk 身份以每任务一次的有限元数据关联，不把调用者 zone 跨线程析构，不用虚构父子栈表示异步关系。
- 线程名按真实池线程身份设置一次，不能把 chunkIndex 当线程编号；线程数量由实际有任务区间的线程去重。
- ThreadPool 的休眠等待、取任务、执行、完成通知可分别标记；不改为 TracyLockable，不改变锁与异常排空规则。
- `enqueue→task start` 只能称观测的排队/调度间隔；等待 zone 和任务尾差不是纯 CPU 执行时间。没有 OS 调度事件时，不能将轨迹空白全部归因于线程池或算法依赖。

Tracy 自身传输/压缩线程与算法线程分别标识；其开销属于插桩扰动。跨线程区间不可简单相加为帧耗时；函数 self time 只扣已标记的同线程子区间，未标记下层函数仍计入 self，不等同于 perf 的叶级 CPU 样本。

### 7.3 关闭与生命周期

关闭宏必须不求值参数、不分配、不查询时间、不引入线程局部初始化或全局注册；普通目标不链接客户端。启用但未连接仍可能有后台线程和 bookkeeping，必须测量，不能叫零开销。

元数据使用静态名称；动态场景/轮次/batch 标识只在 ROI 或每任务边界写一次，不在热循环拼字符串。作用域嵌套、异常退出与任务排空均遵循 Tracy 的 zone 生命周期；禁止悬空字符串与跨线程 zone。[官方作用域和连接约束](https://github.com/wolfpld/tracy/blob/v0.14.1/manual/tracy.tex)

## 8. 采集窗口、程序退出和原始证据

### 8.1 perf 的显式 ROI

runner 创建专属控制/应答通道，以 events-disabled 状态启动 perf 和目标进程。探针完成输入、初建和正常线程池准备后，进入每轮完整更新前请求 enable 并等待 ack；更新结束后请求 disable 并等待 ack，再写文件/运行独立验证。控制握手和标记成本单列，正式无采集路径不执行握手。

采集命令形态为：

```text
perf record -e cpu-clock:u -F 499 --call-graph dwarf,16384
  --delay=-1 --control=fifo:<control>,<ack> -o <perf.data>
  -- <existing-probe> <frozen-input> <profile-mode> <fresh-output>
```

上面是目标接口示意，不是当前可运行的新命令。FPR-01/02 按安装工具版本核对实际参数，并记录完整 argv。继承所有算法子线程事件，不使用会自动禁用继承的配置；用解析并行负载验证不是只录到主线程。CPU clock 是 CPU 上运行的采样，不是阻塞墙钟。[perf 窗口控制与调用栈选项](https://man7.org/linux/man-pages/man1/perf-record.1.html)

可选 perf stat 单独采一小组经过验证的事件，如 task-clock、context-switches，以及实际支持的 cycles/instructions。记录 enabled/running 时间、多路复用及不支持标志；不把 `<not supported>` 当零，不从普通 cache-misses 自动推导完整内存流量或带宽瓶颈。

### 8.2 Tracy 连接与完整落盘

默认 capture 与目标均运行在同一 Ubuntu 会话、本机地址。目标在首个 ROI 前最多等待连接 15 秒；未连接则本次采集失败，不悄悄执行完再生成空 trace。连接必须覆盖全部业务 zones 的开始与结束，中途断开标记不完整；不能将重连片段拼为完整一轮。

优先复用 Tracy 自动生命周期；不为手动启动额外侵入算法。正常完成时先结束业务 zones/排空任务，再等待官方客户端/采集端完成传输。短进程确需 `TRACY_NO_EXIT` 时只由 runner 为该次采集设置，并使用外部超时；禁用无连接无限等待。若使用手动生命周期必须配对 Startup/Shutdown 并在所有业务线程停止后关闭，此变化在小规划单独列出理由。

使用同版本 `tracy-capture` 生成 .tracy，`tracy-csvexport` 导出区间与自身/包含子区间统计；工具版本、导出字段及缺失数据写进 manifest。官方 `-u` 展开提供单区间时间/线程信息，聚合字段的百分比不直接当作算法帧占比；报告按自己的 ROI 重算。[capture 源码](https://github.com/wolfpld/tracy/blob/v0.14.1/capture/src/capture.cpp)、[csvexport 源码](https://github.com/wolfpld/tracy/blob/v0.14.1/csvexport/src/csvexport.cpp)

连接启动、最终排空及文件写入均单列，不计入算法 benchmark；它们仍属于采集总成本。runner 限时终止时保留部分文件和错误，明确标记“不完整”，不声明成功。

### 8.3 符号、时钟和产物

保存源码身份、工具版本、编译 flags、可执行文件/共享库 build-id、二进制及所需调试信息身份、线程/进程、输入 SHA、采集事件、频率、展开方式、控制窗口、退出状态和丢失记录。符号后处理只匹配实际二进制，不拿后来重编译文件解析旧 trace。

perf ROI 使用显式时钟域和开始/结束记录，FPR-02 验证其与 perf 时间戳兼容；Tracy 用自身区间/帧标识。禁止将相对 Tracy 时间戳直接与另一进程的 steady_clock 数字对齐。WSL 时钟若出现非单调或明显跨核错位，保留异常并在能力阶段处理，不裁剪负时间美化报告。

输出根为 `benchmark-output/profiling/fpr-xx/<run-id>/`，包含 manifest、能力结果、原始 perf.data/.tracy、官方文本/CSV、窗口/帧标签、算法结果身份、摘要和失败日志。首次采集建议单文件上限 512MiB、采集总墙钟 180 秒，连接/排空各有短超时；collector 内存单独监测。上游 `-m` 等参数单位以冻结版本为准，不把百分比误当字节。

原始大文件由现有 benchmark-output 忽略规则管理，不进入 Git。报告引用可重建路径与 SHA，不覆盖旧目录，不自动删除失败或较慢数据；工具缓存不写项目业务资源。

## 9. 实施阶段与独立验收

每个阶段先写小规划，完成该阶段代码/文档、必要快验、事实和审查后再考虑下一段；用户已授权小规划自主闭环。前三阶段必须有实际工具产物，不能只写配置。

| 阶段 | 范围与主要文件 | 验收及停止 |
| --- | --- | --- |
| **FPR-01 环境、版本和可复现构建** | 环境检测、锁定依赖、Profiling.cmake/选项、最小解析客户端及工具启动能力 | 确认用户态软件采样、带符号调用栈、真实子线程覆盖；Tracy capture 可连接并读回一个完整短 trace。硬件/调度事件单列可选。默认关闭无新增链接/线程；软件事件不通则不宣称 perf 可用 |
| **FPR-02 函数采样与窗口 runner** | ProfileSession、perf_backend、runner、探针窗口和第一版报告 | 对当前两场景 C4 取得真实函数 self/inclusive 列表及调用栈，排除初建/JSON 混入。样本不足仅按 §10 有限补采；输出符号/展开/丢失质量，形成后续标记清单，不改算法 |
| **FPR-03 Tracy 函数/任务时序接入** | CpuProfiling、tracy_backend、选定算法/线程池标记、导出和报告 | 同场景真实 C4 时间线，能解释主线程等待、实际任务分布和函数区间。检验异常排空、早退、断连、不完整 trace；测关闭/未连接/采集开销。仅一组有证据的细函数层级，不全面插桩 |
| **FPR-04 实际归因闭环与使用契约** | 结果报告、两探针共用入口、必要家族阶段标记及使用文档 | 复用有效采集，必要时补一个匹配串行或家族案例解释具体问题；交付优化候选证据表与 GWR 承接建议。不能以程序可运行替代真实热点分析，不自动开始优化或平台开发 |

FPR-01 如需独立 perf 源码构建，仅尝试已冻结一个来源，不为安装工具重建内核或追逐所有可选功能。FPR-03 若 Tracy 开销过高，先缩小标记粒度，不直接更换算法。硬件事件和系统调度追踪缺失不阻断软件采样+CPU zones；但缺失项不得被其他指标冒称补齐。

## 10. 最小采集与验证契约

### 10.1 输入、重复与样本充分性

沿用 GTP 的两场景、尺度、Q、预算及八轮 `14,14,14,15,16,17,18,14`。每轮所有正常更新工作保持；初始化和独立全量核查分别标记。第一批采集优先 C4：每场景 perf、Tracy 各一个进程，不同时运行；不增加线程档位、场景或压力路径。

函数采样可能需要比墙钟快验更长的观察。先采原八轮；若 ROI 有效样本不足以支持函数排序，允许一次有明确目的的补采：在同一进程中，从同一冻结 seed 独立重建后重复完全相同的八轮，重建位于 ROI 外。由首轮样本数量估算所需重复并在补采前冻结，最多 32 次重放或 180 秒总墙钟，任一到达即停止。

建议用总 ROI 有效应用样本 2000 作为粗热点排序的充分性筛查；单函数不足 30 个自身样本只作低置信线索，不报精确比例胜负。它们不是统计显著性定理。达到上限仍不足则写“不足”，不以增加空转、重复已收敛帧或选择更忙片段补齐。重放仅提高 profiler 观察量，不是正式独立进程统计，也不作为新 workload 胜出证据。

Tracy 默认只需原八轮的一次完整记录；它按事件记录，不为凑同样的样本数重放。串行/家族采集仅因需要解释某个实测现象而增加对应小案例。没有泛化或交叉点结论，不运行四类算法×全部工具×全部线程的完整矩阵。

### 10.2 采集质量与意义

- perf 须报告实际事件、总/ROI 样本、丢失/节流、未知符号占比、无有效调用者路径占比和业务线程覆盖。主要热点落在 unknown 或丢失严重时，归因出口未通过，不能按现有少量可见函数排名。
- 函数自身占比与含下层调用占比分开；父子含时不能相加，总体多线程 CPU 样本也不等于单帧关键路径。
- perf annotate 用于定位某个热点函数中的源行/指令，不解释为每行精确耗时；内联后的归属以实际调试信息说明。
- Tracy 需要完整 ROI、调用栈式 zone 嵌套、真实线程区间、同批 metadata 和结束标志；任务完成顺序不能被用作算法决策输入。
- 无 OS 调度数据时只报告应用可观察的等待/空档，不断言 off-CPU 的根因。硬件计数不能用软件 CPU clock 替代后继续称 cycles。
- 同帧算法结果、事务、预算、实际几何、下一轮状态与无采集路径一致；profiling 模式不得顺手启用原来的全量正确性/细粒度旧探针。

### 10.3 插桩开销和关闭门禁

每个涉及运行时的阶段保留修改前后二进制、相关快验和报告；纯环境文档阶段不跑无关算法。关闭路径检查预处理/编译命令及链接产物，确认宏参数不执行、客户端符号/线程不出现，普通构建无依赖下载。

同环境短对照分开记录：普通 Release 前后、符号构建无采集/有 perf、Tracy 构建未连接/已采集。优先复用已有同条件结果，不将不同构建的差异全归于采样器。进程 CPU 读数包含 Tracy 的后台线程，不能充当算法线程之和。

关闭路径按开发规范 `max(0.05ms,5%×基线,已知波动)` 检查实质回归；缺乏波动数据如实注明，越线仅定向复测一次。基础 Tracy 采集以不超过 `max(0.1ms,10%×对应无采集帧时间)` 的扰动作为建议筛查线；越线先减标记/事件量。超过该线的 trace 可保留用于结构诊断，但不能将区间数值直接当原程序精确耗时。细节级一律另标，不能事后扣一个固定 overhead 得出真实时间。

所有采集在同一存活 WSL 会话中顺序执行，记录启动后稳定状态，避免再次混入 WSL 启动后台任务。控制端/GUI 不与目标同时跑重分析；首版用 headless capture，查看器在采集后打开。性能数字最终由不带 profiler 的构建复测，profiler 只决定下一步值得测什么。

### 10.4 程序化验证集合

最小解析负载含两个已知调用关系的函数、一个真实并行任务段、一个等待段和一个异常退出；仅该夹具可防止被优化掉的专用函数，以验证采样/符号，不把 noinline 带入业务。验证全线程采样、zone 同线程结束、帧与工作区间完整、异常后排空。

会话测试覆盖：工具缺失、事件失败、无连接/中途断连、控制通道无 ack、子进程超时、已有目录拒绝、trace 不完整、符号身份不匹配、采样丢失与缺失字段。优先用保存的小型官方导出样例测试解析，不伪造自然结果。

只构建 profiler 相关目标与受影响的原型/家族测试。线程池源被标记时，运行其现有异常/同步相关用例；不全量 CTest、不构建两个图形后端、不顺手修历史注释格式。阶段审查核对观察代码没有更改锁、任务粒度、评分或发布控制。

## 11. 报告和实际使用要求

结果文档归 `docs/research/profiling/`，代码事实归 `docs/codebase/profiling/`，阶段审查归 `docs/reviews/profiling/`。维护一份中文使用契约，给出能力检测、独立构建、单次采集、离线查看/导出、常见失败和关闭方式的完整可复现命令。

每次用于决策的报告至少包括：

| 内容 | 输出 |
| --- | --- |
| 范围与质量 | 源码/输入/工具/构建身份，完整 ROI，采集扰动，未知/丢失/不足项 |
| 热点 | self 与 inclusive 前列函数，调用路径、源位置、所属阶段；无法分解的符号单列 |
| 时序 | 原轮次的主线程/任务区间、等待与尾部，实际线程参与；用 .tracy 和区间表保留证据 |
| 工作关联 | 已有调用/样本/约束/事务计数，区分调用多与每次重；perf 本身不提供精确调用次数 |
| 优化建议 | 假设→函数/时序证据→待验证原因→可削减工作→建议承接阶段；不把相关性称因果证明 |

首先检验而非预设：重复样本定位、插值/投影构造、精确回退、全局建序、拟合裁剪、分配/析构、串行归并和等待分别承担多少。若测得热点与 GWR 假设不同，更新后续优先级，不为了原规划继续优化一个占比很小的函数。

后续每项实质 work-reduction 都应先引用本工具链给出的相关热点证据，再在修改后用必要范围复核成本是否转移；不要求每次都重录两工具全部输入。函数级 profiler 是持续开发工具，不是一次性截图任务。

## 12. 风险、备选与批准边界

| 风险 | 处理 |
| --- | --- |
| WSL 无硬件 PMU | 使用已验证软件 CPU 采样，明确没有 CPI/cache 结论；不更换内核救指标 |
| 软件 perf 也不可用 | 停在能力门禁，记录错误/环境依赖；Tracy 可独立有价值，但不算完成 perf 要求 |
| 短窗口或栈展开不足 | 有限相同轨迹重放、符号核查或单一 FP 诊断变体；达到配额仍不足则明确限制 |
| 插桩改变调度和热点 | 分离 perf/Tracy，保持基础和细节两层、无采集参照；实际速度只看关闭路径 |
| 捕获连接丢失/尾部未发送 | 连接门禁、有限排空、完整结束标志；保留部分文件不冒充完整数据 |
| 默认目标被污染 | 目标级编译定义、统一宏、一个客户端；默认关闭无链接、无启动、无参数求值 |
| shared pool 标记误改算法 | 标记只在原作用域，不能替换锁类型或重排任务；并发回归定向验证 |
| 工具工作反过来膨胀 | 官方采集/导出、薄适配、四阶段、有界实测，不写自有采样器或通用 trace 数据库 |

建议批准的架构为：公共零求值关闭标记层、工具层 ROI 会话、外部 perf/Tracy 双后端、独立数据报告；Tracy v0.14.1 固定、perf 事件实际探测、默认软件采样和 CPU zones；先有真实热点再补细标记，不在同轮实施 GWR。

## 13. 当前实现情况

规划撰写时只完成只读核查与[规划审查](../../reviews/profiling/cpu_function_profiling_plan_review.md)，随后用户批准实施并先提交文档为 `77ef897`。

FPR-01 已完成[能力闭环](../../research/profiling/fpr_01_capability_report.md)：perf 软件/硬件事件可打开，FP 187 个样本恢复三个线程调用栈，Tracy 完整捕获并读回解析区间。DWARF 子线程展开失败保留限制，依 §5 条件变体改用 FP；原始采集写 ext4 后归档。默认原型二进制未变，业务标记与自然窗口尚未接入。FPR-02～04 按顺序继续；GWR 暂停。
