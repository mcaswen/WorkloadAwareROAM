# FPR-02 自然函数采样结果

2026-09-14。上游为 `081c90f`；当前修改的逐文件身份、实际带符号程序、编译参数和完整命令保存在各采集目录。范围仅为既有 sample14 来源的八轮 C4 轨迹；初始化、JSON、独立质量诊断不在 ROI。GWR 未实施。

## 数据质量

| 场景 | 同种子重放 | ROI 样本 | ROI 外 | 缺栈 / 未知自身 / 丢失 | 实际业务线程 |
| --- | ---: | ---: | ---: | --- | ---: |
| test129 | 17 | 2558 | 1 | 0 / 1 / 0 | 主线程与 4 个池线程 |
| Peking | 16 | 2508 | 0 | 0 / 0 / 0 | 主线程与 4 个池线程 |

初次 152/162 样本决定唯一一次补采规模。重放用于采样量，不是独立研究重复。主表为 `cpu-clock:u` period 加权的机器函数符号，禁用 inline 展开；完整调试信息保留。FP 业务调用链有效，但部分 libstdc++ 外层入口未知；这不等于所有库层级完整，统计也没有删除未知样本。DWARF 子线程问题仍按 FPR-01 的限制处理。

## 实际函数证据

| 场景 | 观测 | 解释边界 |
| --- | --- | --- |
| test129 | `__nextafter` 自身 17.44%；`Measure` 含调用 42.96%，其下 `ErrorBounds` 41.36%；`Donor` 40.03% | 区间算术与精确数值认证是实测热点；父子百分比不可相加，尚未证明可安全删除这些计算 |
| test129 | `IsBoundary` 自身 3.09%，`Weights` 3.01%，另有多项 Boost 浮点/有理数转换、除法 | 拟合、定位和重复事实复用值得分开测；不能只依据源码认定 ClipPolygon 是第一热点 |
| Peking | `Project` 自身 22.09%，两段 PrepareView 内核 16.91% / 7.85%，`PublishView` 自身 8.81% | dense-Q 视图工作已有直接函数证据；相机静止与运动轮次需用时间线区分 |
| Peking | `IsBoundary` 自身 13.44%；`BuildOrders` 含调用 15.55% | 建序成本包括边界资格查询，不能只归因为有序树分配 |
| Peking | `Fit` 含调用 7.42%，`Measure` 1.48% | 场景热点不同；test129 的认证结论不能直接当 Peking 的主瓶颈 |

`SetView` 的主线程调用栈不包含派发后在其他线程执行的 CPU 样本。不能用单线程 inclusive 占比代表整个并行阶段的成本。后续 Tracy 必须覆盖 PrepareView、BuildOrders、PublishView、Receivers、Donor、Fit、Measure、派发任务和等待；不逐样本标记 ErrorBounds/Weights。

完整前 50 自身、含调用和路径表见忽略目录内 `artifacts/summary.json` 与 `report.md`；`summary-current.json` 仅补充未知祖先计数，来自相同官方导出，无重新采样。

## 结果与扰动

全部 136 + 128 帧，与对应同一 FP 程序的无采集轨迹逐帧核对：mesh 文件字节一致，summary 删除 `seconds` 后全部字段一致，包括工作计数、意图和交换。未通过打开诊断取得这一结论。

| 对照 | test129 均值 / 中位数 / P95 ms | Peking 均值 / 中位数 / P95 ms |
| --- | --- | --- |
| FP 无采集，单进程八轮 | 17.814 / 14.984 / 33.381 | 24.086 / 31.697 / 43.590 |
| perf 窗口墙钟合计 / 帧数 | 2611.074 / 136 = 19.199 | 2885.517 / 128 = 22.543 |

两行不是同样重复数的正式性能配对，不能据此扣除采样开销或声称加速。控制握手另耗 2408.726 / 2188.429ms，全部在 frame_update 之外；这是测量工具税，不是算法工作。

普通 Release 新探针初测均值约 21.1ms，超过此前 FPR-01 的 17.116ms。按规则进行唯一一次相邻旧/新复测：

| 程序 | 均值 ms | 中位数 ms | 最近秩 P95 ms |
| --- | ---: | ---: | ---: |
| 修改前备份 | 17.663 | 15.155 | 31.270 |
| 修改后 | 19.643 | 16.149 | 36.419 |

相对变化约 +11.2%、+6.6%、+16.5%，不能宣称关闭路径性能无变化。原 GTP 核心对象未重编译，算法文件无改动；额外控制调用只在显式 profile 模式执行。认证与样本恢复多项同时上涨，因此窗口空指针检查不是已证实原因；链接布局、调度和短跑波动尚未分离。该风险由 FPR-03 的关闭/未连接/采集比较承接，保持旧备份为参照，不展开优化算法或重复性能矩阵。详见[审查](../../reviews/profiling/fpr_02_function_sampling_review.md)。

## 产物

`benchmark-output/profiling/fpr-02/`：`test129-sampled`、`peking-sampled`、`fp-unprofiled`、`normal-after`、`normal-recheck`、`comparison.json`。失败 `test129-first` 与两份初采保留；原始 perf 文件在 Linux 原生缓存写入后归档。每次目录包括 manifest、source-files、recorded-executable、CMakeCache、compile_commands、官方 report/script/header/build-id、窗口与程序输出。

定向验证覆盖四项 Python 聚合/失败测试和实际会话的纯 ack、分段 NUL、错误应答；此前能力夹具继续复用。没有重跑图形后端或家族矩阵。
