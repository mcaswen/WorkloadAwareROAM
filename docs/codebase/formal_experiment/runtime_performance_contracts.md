# 普通运行性能比较：实现事实与实验契约

> 扫描日期：2026-09-10\
> 依据：[性能修复规划](../../plans/formal_experiment/prep_04_runtime_performance_fix_plan.md)；范围为三个采集/分析脚本、测试 Probe 和对应测试\
> 状态：测量实现已形成；历史性能问题经用户决定暂时关闭，拓扑等价性仍影响 PREP-04 验收

## 1. 职责与文件

| 路径 | 入口与主要状态 | 职责及依赖 |
| --- | --- | --- |
| `scripts/compare_runtime_performance.py` | `run_attempt`、`run_process`、`schedule`、`normalize_config`、`capture_identities`、`command_for`；拥有尝试目录、配置、进程和完成记录 | 编排普通矩阵及 Probe，依赖环境模块、分析模块和已有 `cpu_pilot_support`；不决定 CPU 策略优劣 |
| `scripts/runtime_performance_environment.py` | `WindowsEnvironment`；API 绑定和当前采集器句柄；`ProcessMemory`、`ProcessorInfo`、`ProcessorDetail`、`CacheDescriptor` 为 Windows ABI 数据 | 查询处理器组、亲和性、系统/进程时间和内存；作用域修改采集器自身亲和性，子进程继承；不修改生产线程调度 |
| `scripts/runtime_performance_analysis.py` | `summarize_frames`、`paired_statistics`、`analyze_attempt`、`review_micro`、`write_report`；冻结列集与诊断集合 | 从原始 CSV 核查并汇总，以进程配对比较版本；纯分析不启动应用，不读取 DOD 内部状态 |
| `tests/TerrainLodRuntimePerformanceProbe.cpp` | `Run`、`SelectPolicy`、`ValidateGeometry`；栈上算法、场景高度图、逐帧渲染结果 | 通过 `ITerrainLodAlgorithm` 调用生产 DOD，复用清单、相机、公共策略和 CSV 写入；没有渲染器或上传实现 |
| `tests/test_runtime_performance_analysis.py` | `AnalysisTests`；临时 CSV 与完成记录 | 列归属、损坏输入、来源完整性、独立进程及微秒关闭契约 |
| `tests/test_runtime_performance_runner.py` | `RunnerTests`；自有短进程及临时目录 | 成功/失败/超时、亲和性恢复、完整配对顺序及真实 Probe 诊断开关/几何一致性 |
| `tests/CMakeLists.txt` | `parallel_roam_runtime_performance_probe`；两个 Python CTest | 复用 DOD、正式输入源组及项目编译选项；不建立新的生产库 |

实际依赖为：运行器 → 环境/分析/既有身份工具；Probe → 正式输入和公共算法接口 → 生产 DOD。生产模块不依赖 Python 或 Probe。环境模块当前只支持单个 Windows 处理器组；遇到多组会明确失败，不能以截断掩码采样。

## 2. 配置与生命周期

运行器命令为 `python scripts/compare_runtime_performance.py --config <json> --output <新目录> --cohort <批次名>`。配置协议为 1，`versions.old/new` 显式提供 `executable`、`sourceArchive` 和 `buildCache`；Probe 额外提供 `driver`。每个配置组包含 `id/backend/profile/policy/mode/comparison/layout/affinity`，Probe 再提供 `scenarios/cameras`。`comparison` 为 AB、AA 或 BB；`layout` 为 pair 或 abba；处理器条件为 all、实际查询的 `l3:索引` 或显式整数掩码。

```text
创建独占尝试目录，写 running
→ 校验矩阵、版本、资产、源码及基础环境
→ 一次预热块 + 五次计量块，组顺序交替反转
→ 子进程继承作用域亲和性，记录实际掩码、PID 和起止时间
→ 回收进程，读取 CPU 时间、周期、峰值工作集和系统忙碌比例
→ 校验原始 CSV，记录文件摘要与帧数量
→ 核对运行前后来源/环境；完整一致才写 complete
→ 独立分析；失败记录保留，不进入速度统计
```

单进程目录由运行器创建，已有目录不会覆盖。超时/异常先终止并回收直接子进程，再恢复采集器亲和性。失败尝试的元数据和日志留在原目录，不能仅凭 CSV 存在判断成功。原始 CSV 为事实源；`runs.json` 是进程来源索引；`analysis.json` 和中文报告均可从成功尝试重建。

## 3. Probe 的边界

Probe 先验证完整场景与相机清单，再选定一个场景重放。算法对象跨这 64 帧保留状态，每次独立进程重新创建；相机在构建前转换，结果校验、输入与几何哈希以及 CSV 写入在构建计时之后。`TerrainLodRenderPacket` 的借用网格在下一次构建前完成读取，不越过公共生命周期。

`diagnostics-off` 固定阶段证据/拓扑验证/拓扑配对证据为 false/false/false；`diagnostics-on` 固定 true/false/false。列名沿用 `passEvidenceEnabled/topologyValidationEnabled/topologyPairEvidenceEnabled`，每帧另有 `probeProtocolVersion=1`、模式与相机身份。预设只覆盖执行动作，清单冻结的线程数量和并行门槛继续生效。

`ValidateGeometry` 检查索引、有限位置/法线/纹理坐标/高度；将每个三角形循环旋转到字典序最小起点，保留绕序，再排序三角形并编码摘要。它不读取路径编号或 DOD 拓扑；可以发现诊断开/关及版本之间的几何变化，不等于完整拓扑证明。调试颜色不属于此几何摘要。公共结果验证在读取借用网格前执行。

外围验证固定在每帧构建后，仍可能影响下一帧缓存状态；两版本严格使用同协议。这是 CPU 公共调用对照，不是 GUI 帧率、上传吞吐或无外围验证的运行成本。

## 4. 分析与独立单位

`BENCHMARK_FIELDS` 固定普通 CSV v4 的完整顺序，`PROBE_FIELDS` 复用其公共设置/统计列并替换入口字段。未知、缺失、重复或重排列均被拒绝。`TIME_FIELDS` 显式编码计时归属，`cpuUtilizationPercent` 是环境观测，剩余已知列全部参与算法语义摘要。诊断开关不符、失败结果、非有限/负计量、错误序号与不完整帧集合不能进入分析。

每个进程先汇总各计时列的中位数与 P95，再形成 B−A 配对差。ABBA/BAAB 每块有两个相邻进程对，同时保存两项进程差及其块平均；块不作为额外样本重复计数。报告保留每版本原值、进程数、预热数、块数、进程差的中位数、中位数之差、相对差和 A 极差，两类中位数运算不相互替代。

`ALIASES` 记录共同包络的别名，不能累加别名声称总成本。普通极差调查规则与 CPU crossover 的策略胜负规则互不依赖。离线分析核对文件摘要、实际亲和性、标签/程序身份、同组优先级、重复来源、预热位置及算法语义；不同环境条件分别汇总。

通用报告中的 `aRange/investigate` 使用本批次同期 A 进程的极差，只描述该批观测。原始 PREP-04 五次基线极差与 25 个触发项保存在独立调查记录，逐项复核时继续列出，不能用较宽的同期极差替换原阈值或自动关闭历史问题。

`review_micro` 仅接受毫秒计时列，拒绝进程秒数及内存值；要求两个不同尝试、每批五个 ABBA/BAAB 计量块、AB/AA/BB 各 10+10 个计量进程，输入/结果/环境/版本身份一致。各批版本配对中位差及中位数之差均在 ±0.01 ms 内，版本配对中位差落在同程序绝对差 P95 内，且该 P95 不超过 0.01 ms，才允许限定的“无版本相关证据”关闭。全部进程差与控制范围仍保留，不能据此证明数学等价。

进程 `seconds` 当前从作用域亲和性/系统查询前计到等待结束，包含进程启动与外围管理；CPU 时间与峰值工作集单列。它与原调查器的外围边界不完全相同，跨协议历史进程秒数只保留为参考，本轮同期版本比较采用相同边界。中文报告显式列出两种中位差，调查标记使用中位数之差，不能从配对中位差的正负直接推导。

## 5. 性能与未决事项

FACT：2026-09-10 用户暂时关闭本轮性能问题并允许继续拓扑修复；开发规范第 7.3 节定义后续更宽的工程影响门槛。`review_micro` 和报告中的 `investigate` 仍保留旧协议语义，用于历史审计，不能把旧极差触发项直接作为新阶段的自动阻断条件。后续报告需显式按新工程门槛作处置，研究胜负标准不变。

FACT：生产 DOD、普通 Benchmark CLI 及两类渲染上传实现没有在本修复中改动。独立调查入口有排序、哈希与 CSV 成本，这些成本显式属于外围验证或离线分析。构建重新链接的应用与保留程序通过不同摘要区分。

FACT：新增 8 个分析案例及 4 个进程/真实 Probe 案例完成，两后端最终专项含注释覆盖。16 个成功性能尝试共 1,488 个应用进程，最终只读重分析与各次报告一致；一个 144 进程批次因源码身份变化被整体排除，未增加有效样本。两后端/两场景/两策略/两版本的跨诊断输入与几何核对均一致。最终分析器处理同一份 144 进程数据约 0.8561 秒、37.9 MiB，属于工具成本。

FACT：原 25 项触发中，最大并行合并建堆中位数在全部处理器的两批控制下符合限定关闭；细分建堆等项未满足。仅在新处理器条件下满足的项不替代原环境验收。原始/保留/重建及隔离链接产物的对照各自有独立身份，当前生产链接预设未修改。

UNCERTAIN：处理器调度、频率和缓存对历史异常的逐项贡献；低于同程序分辨能力的微小差值是否具有跨机器一致根因。系统忙碌比例包含被测进程本身，不能解释为后台负载比例；进程 CPU 周期不等于每核频率或线程迁移轨迹。具体性能结论与逐项关闭状态以[性能调查](../../reviews/formal_experiment/prep_04_runtime_performance_regression_analysis.md)及实际产物为准。
