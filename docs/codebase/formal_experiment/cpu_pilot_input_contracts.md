# CPU pilot 输入与记录代码事实

> 本文保留 PREP-02 的输入冻结与记录基线。输入清单契约继续适用；后续实现的配对 v2、执行追溯和分析契约见 [CPU 阶段配对契约](cpu_pass_pairing_contracts.md)。本文当时标记为待实现的配对能力以新文档为准。

> 日期：2026-09-09\
> 范围：PREP-02 的共享输入、无窗口准备入口和 Python 摘要脚本\
> 源码状态：实现与注释整改已提交为 `72f9c5f`，父提交为 `06cb24c`；不代表正式采样完成\
> 规划：[PREP-02](../../plans/formal_experiment/prep_02_minimal_input_plan.md)；上游事实：[CPU 策略与回放](cpu_policy_and_replay_baseline.md)

## 1. 模块、依赖和生命周期

FACT：`src/experiment/formal` 的命名空间为 `ParallelRoam::Experiment::Formal`，负责场景、相机、目标的值类型及读写/校验，另提供输入摘要、CPU 发现和配对的最小记录写入器。它直接依赖公共 LOD 设置、阶段枚举、哈希、视图构建和 HeightMap，不包含 DOD 内部状态、App 或图形 API。

FACT：`src/benchmark/formal/FormalExperimentRunner.cpp` 的 `PrepareCpuPilotInputs` 是当前唯一生产调用入口。既有 `RunTerrainLodBenchmark` 遇 `BenchmarkProfile::CpuPilotInputs` 时在创建普通场景之前转入此函数。它不执行 DOD 更新、工作负载发现、目标选择或策略计时。

FACT：清单集合、临时索引、生成相机和输出流均由当前同步调用持有。没有持久服务、缓存、后台任务、线程池、GPU 资源或跨帧状态。函数返回或抛出后自动释放局部对象；失败输出保留在独占的新目录，供诊断使用。

FACT：`scripts/prepare_cpu_pilot.py` 只准备一个尝试，负责 Python 标准库 SHA-256、Git/构建/环境记录、冻结清单副本和应用子进程。C++ 的目录/摘要逻辑只覆盖输入准备，没有实现正式 RunStore 或自动恢复。

## 2. 文件和符号索引

以下路径相对仓库根目录。

| 文件 | 类型或核心符号 | 真实职责与调用关系 |
| --- | --- | --- |
| `src/experiment/ExperimentCsvCodec.h/.cpp` | `ExperimentCsvRow`、`ExperimentCsvTable`、`ReadExperimentCsv`、`WriteExperimentCsvRow` | 共享 CSV 词法、UTF-8、表头/行宽检查和区域无关数值转换；清单和记录层调用 |
| `src/experiment/formal/FormalExperimentTypes.h` | `FormalScenario`、`CameraSample`、`TargetStateRef`、`FormalInputRequest`、`CpuPilotPassIds` | 无资源所有权的公共值类型；复用公共设置与阶段枚举 |
| `src/experiment/formal/FormalExperimentCamera.h/.cpp` | `GenerateCameraSample(s)`、`BuildCameraView`、`ValidateCameraSample` | 轨迹 A 公式、NO 视图、FNV 身份及重建校验 |
| `src/experiment/formal/FormalExperimentManifest.h/.cpp` | `Load/WriteScenarioManifest`、`Load/WriteCameraManifest`、`Load/WriteTargetManifest` | 三种 pilot schema 的协议和关系边界；读失败不返回部分集合 |
| 同上，内部符号 | `RowReader`、`ReadTable`、`ParseScenario`、`ValidateScenario`、`IndexScenarios`、`ParsePass`、`ValidateFeatureVector` | 数值行转换、固定协议检查、索引和五阶段/特征向量语义；不承担通用 CSV 词法 |
| `src/experiment/formal/FormalExperimentRecords.h` | `InputPreparationSummary`、`CpuDiscoveryRecord`、`CpuPairRecord`、`CpuRecordStatus` | 分离输入准备与发现/配对的输出数据；记录默认 incomplete 或 preparing |
| `src/experiment/formal/FormalExperimentCsv.h/.cpp` | `WriteInputPreparationSummary`、`WriteCpuDiscoveryCsvHeader/Row`、`WriteCpuPairCsvHeader/Row` | 独立 pilot schema 和最小记录输出，不使用普通共享 CSV v4 或旧配对 CSV v2 |
| `src/benchmark/formal/FormalExperimentRunner.h/.cpp` | `PrepareCpuPilotInputs`、内部 `WriteFile` | 新目录、加载/生成/落盘重读、状态摘要和失败返回 |
| `src/benchmark/TerrainLodBenchmark.h` | `BenchmarkProfile::CpuPilotInputs`、`BenchmarkOptions::FormalInput` | 新配置与共享请求的持有位置，不逐项复制场景设置 |
| `src/benchmark/TerrainLodBenchmarkCommandLine.cpp` | `ParseTerrainLodBenchmarkCommandLine`、`ParseProfile`、`BenchmarkUsage` | 新参数、缺值/重复 ID/模式冲突检查；不创建目录或读取清单 |
| `src/benchmark/TerrainLodBenchmark.cpp` | `RunTerrainLodBenchmark`、内部 `ToString` | 新配置名称和早期分流；旧场景/回放实现未改 |
| `scripts/prepare_cpu_pilot.py` | `prepare`、`file_identity`、`source_identity`、`selected_assets`、`basic_environment`、`read_rows`、`write_metadata`、main | 计时外摘要、环境、进程、数量核对与尝试状态 |
| `docs/parallel-roam/cpu-pilot-scenarios-v1.csv` | 六个轨迹 A 场景 | 两地形各三预算；用途固定 exploratory；独立于未来十八场景正式清单 |
| 根 `CMakeLists.txt` | `PARALLEL_ROAM_FORMAL_INPUT_SOURCES` | 四个共享实现文件供两后端与 CPU 测试使用；应用另注册 Runner |
| `tests/CMakeLists.txt` | 四项新增测试 | 三项 C++ 测试只链接公共选项、GLM/STB；进程测试调用真实应用和 Python |

## 3. 输入模型与不变量

### 3.1 场景

FACT：`FormalScenario` 保存 `ScenarioId`、`TerrainId`、资产路径/宽高/SHA-256 声明、公共 `TerrainLodSettings`、轨迹/分组、采样数量、drawable、FOV/近远面、串并行请求线程及预热/重复设置。加载后高度图路径转为基于 `assetRoot` 的绝对规范化路径。

FACT：`ValidateScenario` 只接受 `test129` 或 `peking547`，轨迹 A、train。三种合法预算分别为 512/4096/20000 和 20000/80000/200000；场景 ID 必须精确对应。两地形的尺寸、缩放、深度、像素阈值和 SHA-256 声明按 24 号协议逐项比较。相机必须是 64 点、1280×720、60°、0.1/500；局部约束 true，五个数量下限 0，串行 1、并行请求 8，预热 5、记录 30。

FACT：这些重复设置当前仅被保存，输入准备不使用它们执行任何采样。未来 pilot 的实际重复数量和覆盖参数由 PREP-04/05 定义，不能从本阶段清单推断已经采集了 5/30 次配对。

FACT：`ParseScenario` 将公共策略设为串行增量，五个阶段线程明确为 1，关闭 `EnableParallelSplit`，开启阶段证据和拓扑验证；内部拓扑配对证据保持默认 false。它是后续来源状态重建输入，不是计时副本策略。

FACT：场景文件全部行先经过协议与身份检查；只有被选中的行才实际加载高度图。显式请求的 ID 必须全部存在，重复 ID 或重复场景失败。返回集合按场景 ID 升序。实际解码复用 `HeightMap::LoadFromFile`，核对 129×129 / 547×547；C++ 不计算文件 SHA-256。

### 3.2 相机

FACT：`CameraSample` 保存场景/地形/轨迹/采样编号、position/target/up、FOV/裁剪/drawable，以及 `CameraPoseHash`、`ViewMatrixHash`、`ProjectionNoHash`、`ViewInputHash`。没有 yaw、pitch、帧时间或预算字段。

FACT：`GenerateCameraSample` 采用协议 A 的 float 运算：`k/63`、`radians(-5+10*t)`、半径 `0.85R`、高度 `0.30R`、目标高度 `0.05R`。朝向接近竖直时采用 `(0,0,-1)`，否则采用 `(0,1,0)`。B/C 及采样编号 64 以上直接失败。

FACT：`BuildCameraView` 检查有限值、非退化方向/up、正 drawable 与合法投影参数，显式调用 `lookAtRH`、`perspectiveRH_NO`，再复用 `BuildTerrainLodViewInput(..., false)`。该函数消费行中坐标，不重新计算轨迹公式。

FACT：姿态哈希依次追加 position、target、up 的 x/y/z 位模式。矩阵哈希按列再按行追加 16 个 float。NO 输入哈希从公共 FNV offset 开始，追加 `cpu-pilot-view-no-v1`、drawable、FOV/裁剪、三个派生哈希。编译期要求小端和 4 字节 IEEE float；不把 FNV 当作 SHA-256。

FACT：加载相机时同时核对从实际行重算的哈希和生成器的预期完整行。重复 `(scenarioId,sampleIndex)`、不匹配场景、非 NO 深度或任何缺点均失败；合法行按场景/编号排序。`ValidateCameraSample` 与默认值类型相等比较共同检查标量、姿态和身份；单个 ULP 变动也被拒绝。

### 3.3 目标

FACT：`TargetStateRef` 的 `PassId` 复用 `TerrainLodPassId`，加载器只接受 `CpuPilotPassIds` 中五个 CPU 值。CSV 名称复用公共 `ToString`：`mergeScore`、`mergeTopology`、`splitScore`、`splitTopology`、`meshEmit`；上传值与未知名称失败。

FACT：目标编号范围 1..63；层名称 low/middle/high/coverage；序号在同场景同阶段内唯一、从 0 连续且最多八个；种子固定 20260830；主工作量必须正数；分号分隔的特征向量不能为空且每项是有限 float；重放和选择特征身份非零；分组 train。目标必须绑定对应相机的姿态及 NO 输入哈希。

FACT：输入相机集合本身也被完整验证，不能只提供目标涉及的几个点。返回目标按场景、公共阶段枚举、选择序号排序。一个目标身份 `(scenarioId,passId,sampleIndex)` 只能出现一次。

UNCERTAIN：此层无法证明 `ReplayInputHash` 与真实 DOD 冻结状态相等，也不重新计算 `SelectionFeatureHash`。它们的实际重建验证属于 PREP-03/04；目前只有外部引用校验。

## 4. CSV 与记录

FACT：CSV 编解码器接受 UTF-8，可带文件开头 BOM；记录分隔接受 LF/CRLF，输出 LF。引号内逗号、双引号和换行通过转义保存。它拒绝 NUL、非法 UTF-8、未闭合/错误位置引号、记录外裸 CR、缺失或重复表头以及行宽不一致。单个空字段是合法词法值，必填语义由清单处理。

FACT：`ReadExperimentCsv` 将当前输入读入局部字节串，再解析到行集合；本阶段输入很小，没有流式大型清单读取器。异常携带来源，词法错误带物理行号，行宽错误带记录号；场景行转换额外带文件及记录号。相机/目标的部分语义错误当前只含错误原因或场景身份，不保证每个错误都有字段号。

FACT：列名和顺序必须与 schema 完全相同。无符号整数用 `from_chars` 完整解析到 uint64；范围更窄的计数字段额外检查。float 采用区域无关的 `from_chars` 和 `to_chars(max_digits10)`；非有限值、溢出、空值和尾随字符失败。时间记录用 double 的 max_digits10。

FACT：三种清单和最小记录当前均为 pilot v1，写入器固定 `dataPurpose=exploratory`，没有用户提供 formal 字符串的接口。未来扩展完整 schema 需要显式升版，不能套用普通 CSV 的版本。

FACT：`CpuDiscoveryRecord` 当前包含身份、执行前工作量、规划工作量、采集耗时、状态和失败信息。`CpuPairRecord` 包含身份、重复/预热、AB/BA 和块内序号、请求/实际动作和线程、回退、输入/结果哈希、耗时和正确性/等价布尔值。写入器检查基本范围，valid 记录必须有必要身份/正确性证据；它不是执行器或统计配对完整性检查器。

PLANNED：发现的阶段专有特征、真正策略执行和计时、配对集合完整性及统计分别由 PREP-03/04 扩展。当前生产入口只写输入摘要，不生成伪造的发现或配对 CSV；这两种记录目前由单元测试消费。

## 5. 控制流和状态变化

### 5.1 C++ 准备入口

```text
main / 既有应用参数分流
→ ParseTerrainLodBenchmarkCommandLine
→ RunTerrainLodBenchmark(CpuPilotInputs)
→ PrepareCpuPilotInputs
  → create_directory（已有目录失败，不写入）
  → input-summary.csv：preparing
  → LoadScenarioManifest（协议、筛选和实际解码）
  → GenerateCameraSamples 或 LoadCameraManifest
  → 可选 LoadTargetManifest
  → 写 scenarios.csv / camera-samples.csv
  → 从实际文件重读相机，核对全部行
  → 可选写/重读 target-states.csv
  → input-summary.csv：inputs_validated
异常 → 自己已创建的目录中写 failed + error，返回 1
```

FACT：C++ 摘要始终写 `assetDigestStatus=external_verification_required`；`inputs_validated` 不代表摘要或实验完成。目标状态依次为 not_provided 或 provided_unchecked，校验成功后为 references_validated。失败时未检查目标不会被写成“未提供”。

FACT：输出流显式 close 并检查写入错误；摘要发布失败也返回非零并打印错误。没有原子发布保证或恢复协议；中断文件必须视为不完整。

### 5.2 Python 准备流程

```text
prepare → 独占新尝试目录 → run-metadata.json：preparing
→ 实际二进制/构建预设归属检查、Git、基本环境、源码/构建文件摘要
→ 将输入清单复制到 manifests/，核对复制字节
→ selected_assets：按选择场景核对资产实际 SHA-256
→ 真实应用子进程（新 inputs/，日志 prepare.log）
→ 检查退出码、摘要、场景/相机数量和目标状态
→ 重新核对原输入、资产、构建文件、源码与 HEAD
→ 记录完整输入/输出摘要 → inputs_ready
任一步异常 → failed + error，保留已写文件与日志
```

FACT：目录通过 `mkdir(..., exist_ok=False)` 获得，包括空目录也拒绝复用；外层拒绝已有目录时不会修改其文件。没有扫描旧成功单元、恢复、删除历史数据或自动重试。

FACT：源码快照来自 Git 跟踪和未忽略的新文件，覆盖 src/tests/scripts/cmake/third_party/tools 及根 CMake、预设和 vcpkg 文件；记录逐文件路径/字节数/SHA-256，缺失的跟踪文件记录 missing。组合摘要来自排序后的 JSON 字节。它包含绝对路径，是当前机器的追溯快照，不是 PREP-09 的跨入口配置指纹。

FACT：构建文件记录可执行文件、相邻 DLL、指定预设的 CMakeCache 与 CMakeCXXCompiler 文件。CLI 要求二进制位于仓库 `build/<build-preset>` 下。实际后端、构建配置与编译器版本来自 C++ 输出，预设标签不能代替这些事实。

FACT：基本环境包括系统、机器架构、处理器描述、逻辑线程数、Python、工作目录及进程；Windows 额外查询 CPU/核心、内存、优先级、亲和性和电源计划。查询失败会保存 `environmentQueryError`，不伪造值；完整 GPU/驱动/图形环境准入尚未实现。

FACT：脚本前后摘要能发现常规输入变动，不提供对恶意并发写入的防护。`inputs_ready` 只描述这次输入准备的完成状态；后续执行前仍需重新核对实际文件身份。

## 6. 命令和测试入口

```powershell
python scripts/prepare_cpu_pilot.py --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe --build-preset relwithdebinfo-fetch --output-dir benchmark-output/prep-02/new-attempt
```

FACT：不传 `--scenario-manifest` 时使用仓库六行 pilot 清单；`--scenario-id` 可多次指定；可选 camera/target 清单用于冻结输入回放校验。直接应用模式为 `--benchmark --profile cpu-pilot-inputs --scenario-manifest <path> --output-dir <new-directory>`，路径按当前工作目录解释，父目录须存在。

FACT：新模式拒绝普通算法、策略、线程和 CSV 覆盖参数；普通模式拒绝清单及输出目录参数。帮助仍按出现位置立即返回；重复场景 ID 拒绝，文件选项重复时最后一次生效，未知正式配置仍失败。

| 测试 | 验证范围 |
| --- | --- |
| `formal_experiment_manifest` | CSV 语法、协议/尺寸、重复/缺失场景、相机损坏、五阶段目标关系、失败/不足记录 |
| `formal_experiment_camera` | 六场景 384 点、协议绝对坐标、预算无关身份、NO 近远面、float 位模式往返与一 ULP 篡改 |
| `formal_experiment_command_line` | 新配置、带空格路径、多个 ID、缺值、普通覆盖冲突和旧模式隔离 |
| `cpu_pilot_preparation` | 真实应用、完整/子集清单、五目标、可独立重算摘要、资产单字节篡改、相机缺行、已有/中断目录 |

实际完整构建和回归结果记录在 [小规划实现情况](../../plans/formal_experiment/prep_02_minimal_input_plan.md#7-实现情况)，不把测试生成输入用于性能结论。

## 7. 变化风险、后续工作与未确认事项

FACT：普通配置、公共算法虚接口、DOD、HeightMap、线程池、渲染器和应用生命周期没有本阶段差异。公共头 `BenchmarkOptions` 多持有一个共享输入请求，旧调用默认构造为空。

INFERENCE：相机公式、字段顺序、FNV 追加类型和矩阵约定变化都会改变冻结身份；需要显式处理 schema 兼容。目标外部引用检查不能代替 DOD 状态验证；提前信任目标哈希会破坏可靠配对。

PLANNED：PREP-03 接入真实阶段边界、发现与目标选择；PREP-04 接入可靠 CPU 配对与最小分析；完整图形输入和正式文件系统仍在 gate 后。当前没有这些生产执行能力。

### Unresolved / Uncertain

- UNCERTAIN：没有验证不同编译器/libm/架构间的相机位级兼容性；当前设计遇不匹配会失败，不覆盖旧清单。
- UNCERTAIN：没有真实 DOD 目标身份或性能交叉结论；本阶段目标测试使用显式合成身份，只证明引用校验。
- FACT：当前新增代码的所有权与静态调用路径可定位；不存在需要猜测的后台或图形资源生命周期。
