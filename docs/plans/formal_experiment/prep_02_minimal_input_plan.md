# PREP-02：最小 CPU 输入与可追溯记录

> 规划类型：小规划\
> 日期：2026-09-09\
> 状态：实现、验证及架构核查完成；实现与注释整改提交为 `72f9c5f`\
> 授权：用户先要求“再写 prep_02 并自主实现”，随后要求补齐注释，并明确许可“跟 prep-01 一样提交一下”\
> 上位规划：[正式实验准备大规划](formal_experiment_preparation_major_plan.md) 第 7.5、8.1、11 节\
> 起点：`06cb24c`；PREP-01 已完成，工作区干净

## 1. 问题、目标与依据

已有无窗口入口只能使用内置场景和相机；CSV 写入器不能解析外部清单，也没有运行输入摘要。此次建立 CPU pilot 的共享输入契约，使后续发现和可靠配对能读取同一份冻结输入。

实施前已阅读开发规范、规划/事实/审查指南、上位规划、PREP-01 小规划及审查、[CPU 事实](../../codebase/formal_experiment/cpu_policy_and_replay_baseline.md)，并核对 [24 号协议](../../parallel-roam/24-formal-experiment-preparation-plan.md) 第 3～5 节。扫描了无窗口参数和运行入口、公共阶段/哈希/视图类型、HeightMap、CSV 与相邻测试、CMake 和脚本。

当前可复用的核心能力为 `BuildTerrainLodViewInput`、公共 FNV-1a 追加函数、`TerrainLodPassId`、`TerrainLodSettings` 和 `HeightMap::LoadFromFile`。不复用普通 CSV 的版本，也不复制 DOD 状态。

完成标准：可以从协议轨迹 A 的六个场景中选择任意非空子集，生成并重新校验每场景完整的 64 个 NO 相机点；目标引用只接受五个 CPU 阶段和有效相机；输入/结果格式明确为 `exploratory`；准备脚本记录可核对的实际文件摘要、命令、线程和环境；错误或中断不会留下可误认为实验完成的标记。

## 2. 范围与边界

- 实现轨迹 A，两地形各三个协议预算，共六行可审查的 pilot 清单。保留 24 号文档的地形、阈值、64 点、投影、线程和五个零下限取值。
- 场景文件使用独立 `cpu-pilot-scenarios-v1.csv` 和 `dataPurpose=exploratory`，不以六行文件冒充正式十八场景清单。
- 相机与目标 CSV 使用独立 pilot schema v1；相机只有 NO 投影，`viewInputHash` 明确代表 NO。B/C、ZO、上传和正式完整 schema 属后续阶段。
- 本阶段生成输入并校验已有目标引用，不生成目标选择结果，不推进 DOD，不执行策略计时。发现和配对的基础记录只定义身份、状态和最小字段，由 PREP-03/04 接入实际生产者。
- 不创建 C++ SHA-256、Fingerprint、RunStore、自动恢复、图形会话或上传组件。

## 3. 架构判断、职责与文件归属

```text
prepare_cpu_pilot.py：独占新尝试目录、文件摘要、命令和环境
  → 无窗口 cpu-pilot-inputs 配置
    → Benchmark/FormalExperimentRunner：顺序输入准备与错误报告
      → Experiment/Formal Manifest / Camera / Csv：共享值类型与校验
        → ExperimentCsvCodec、公共 LOD 视图/哈希、HeightMap
```

这是大规划既定共享层与无窗口编排层的局部落地，不增加新的系统层。`Experiment::Formal` 不依赖 Benchmark、DOD、App 或图形 API；Python 不实现相机公式或目标选择。输入由调用方持有，读取/生成同步完成，无线程池和跨帧资源。

| 操作 | 文件 | 职责与独立存在理由 |
| --- | --- | --- |
| Create | `src/experiment/ExperimentCsvCodec.h/.cpp` | 严格 CSV 词法、UTF-8、引号/换行、列数及数值转换；不理解场景或执行算法 |
| Create | `src/experiment/formal/FormalExperimentTypes.h` | 场景、相机、目标和准备请求的值类型；复用公共设置/阶段枚举 |
| Create | `src/experiment/formal/FormalExperimentCamera.h/.cpp` | 唯一轨迹 A 生成器、NO 视图重建与姿态/矩阵身份；从冻结行重建视图 |
| Create | `src/experiment/formal/FormalExperimentManifest.h/.cpp` | 三类清单的 schema、数值、协议及引用校验；高度图真实尺寸检查；规范化读写 |
| Create | `src/experiment/formal/FormalExperimentRecords.h` | 输入摘要、发现、配对最小记录；与输入模型分离 |
| Create | `src/experiment/formal/FormalExperimentCsv.h/.cpp` | 独立 pilot 记录 schema 与输出；失败和缺行显式保留 |
| Create | `src/benchmark/formal/FormalExperimentRunner.h/.cpp` | 输入准备顺序、独占输出目录、状态摘要与错误退出；不承担通用存储或实验执行 |
| Extend | `src/benchmark/TerrainLodBenchmark.h` | 增加准备配置和共享请求值，不把清单字段逐项复制为普通覆盖项 |
| Extend | `src/benchmark/TerrainLodBenchmarkCommandLine.cpp` | 解析清单/过滤/输出参数并检查普通配置冲突；仍不执行 I/O |
| Extend | `src/benchmark/TerrainLodBenchmark.cpp` | 新配置路由至独立运行器，旧路径保持原语义 |
| Create | `scripts/prepare_cpu_pilot.py` | 使用标准库 hashlib 记录文件/源码摘要、命令、环境、输入输出数量及失败；只准备一个尝试，不调度性能实验 |
| Create | `docs/parallel-roam/cpu-pilot-scenarios-v1.csv` | 六个 A 场景的可审查输入；实际值来自 24 号协议 |
| Extend | 根和 tests 的 `CMakeLists.txt` | 注册共享纯 CPU 源列表和针对性测试，两后端使用同一源文件 |
| Create | `tests/ExperimentManifestTests.cpp` | CSV、场景协议、尺寸、相机/目标关系及记录格式的语义边界 |
| Create | `tests/ExperimentCameraTests.cpp` | 协议固定点、64 点唯一性、预算无关姿态、binary32 往返及 NO 裁剪 |
| Create | `tests/FormalExperimentCommandLineTests.cpp` | 新配置、缺值、重复/互斥/未知参数与旧模式隔离 |
| Create | `tests/test_cpu_pilot_preparation.py` | 真正进程接入、摘要篡改、缺行/失败、不可覆盖和环境/源码记录 |
| Create/Extend | 本小规划、大规划、模块事实和本阶段审查 | 分别记录目标、阶段进度、真实接口与核查结果 |

`ExperimentCsvCodec` 从输入流读取小型 CSV 字节串，再逐行解析并转换值，清单层组合复用；不引入大型文件流式框架或模板序列化框架。清单采用先解析到局部集合、完整验证后返回的方式，异常不得把半份输入交给后续执行。新增共享源列表复用现有构建方式，不重构整个 CMake。

## 4. 输入和输出契约

### 4.1 场景

`LoadScenarioManifest(path, assetRoot, selectedIds)` 解析并验证非空 A 场景子集，拒绝重复身份、未知列/版本/用途、缺字段和非法数值。路径按显式资产根解析；CLI 使用当前工作目录，准备脚本固定为仓库根。格式化输出保留完整场景字段。

地形与预算组合、尺寸、缩放、深度、阈值、投影和配置必须符合 24 号协议；PGM 为 129×129，Peking PNG 实际为 547×547。协议中的两条 SHA-256 值作为预期身份；C++ 检查清单声明及真实解码尺寸，Python 在启动子进程前和结束后分别检查实际文件内容。裸 C++ 输入校验不能宣称已经完成资产 SHA-256 验证。

场景过滤不根据耗时；要求显式请求的所有 ID 都存在。准备默认读取六个场景，可反复指定 `--scenario-id` 缩小范围。文件顺序规范化为场景 ID 升序，相机按场景与采样编号升序。

### 4.2 相机

每场景恰好 `sampleIndex=0..63`。轨迹公式使用 C++ `float` 和 GLM 的显式 `lookAtRH` / `perspectiveRH_NO`；复用公共视图构建。用 `max_digits10` 写出 binary32，以区域无关方式完整解析；非有限数、溢出和尾随字符失败。

姿态哈希只包含 position/target/up；矩阵按 GLM 列序追加 float 位模式，完整 NO 输入身份包含深度约定、drawable、投影参数、姿态和矩阵。复用现有 FNV 追加实现，pilot 要求 IEEE binary32、小端平台，拒绝把此哈希当作文件 SHA-256。预算不进入姿态/视图身份。

加载相机先检查所有行及引用，再用同一生成器核对冻结值和哈希；运行侧从已校验行重建公共视图，不经过 GUI 的 yaw/pitch。缺点、重复点、混用场景、错误投影或篡改值失败。

### 4.3 目标与记录

目标仅接受公共枚举中的五个 CPU 阶段，`sampleIndex=1..63`；禁止 `cpu-upload` 和根状态 0。保留选择层、序号、种子、主工作量、特征向量、重放/特征身份及 train 分组，并绑定相机姿态和 NO 视图哈希。目标身份 `(scenarioId, passId, sampleIndex)` 唯一；同组选择序号唯一且从 0 连续，最多八个；每组引用完整的冻结相机。

本阶段只能核对目标的外部关系和非零身份，不能证明其 `replayInputHash` 等于真实 DOD 冻结状态；后者必须由 PREP-03/04 重建核对。未提供目标清单时明确记录 `not_provided`，不生成空目标冒充已选择。

最小发现记录包含场景/采样/阶段/输入身份、执行前工作量和规划工作量、采集成本与状态；最小配对记录包含目标、重复和块内次序、请求/实际动作及线程、回退、耗时、输入/结果身份及正确性。写出器强制 `dataPurpose=exploratory`，不接受调用方改成 formal；阶段选择器后续只接收允许字段的窄类型。

### 4.4 命令和追溯

新增无窗口配置：

```text
--benchmark --profile cpu-pilot-inputs
  --scenario-manifest <path> --output-dir <new-directory>
  [--scenario-id <id>]... [--camera-manifest <path>] [--target-manifest <path>]
```

不提供相机清单时生成，提供时读取校验后规范化输出。普通算法/策略/线程/CSV 覆盖项与该配置互斥，避免静默覆盖清单。其他正式配置继续拒绝；已有合法 CLI 和帮助行为保留。

推荐通过 `python scripts/prepare_cpu_pilot.py --executable <path> --build-preset <name> --output-dir <new-attempt>` 调用。脚本记录实际命令数组、Git HEAD/状态、包含未提交源文件的逐文件 SHA-256 与组合摘要、可执行文件/相邻运行库/构建缓存摘要、场景/相机/目标/资产摘要、场景线程设置、CPU/系统/逻辑线程数量和时间。记录实际后端和构建配置，不把参数标签当作运行事实。

父尝试目录和 C++ 的 inputs 子目录分别以独占创建方式获得；已有目录即失败，包括空目录。脚本初始状态为 `preparing`，成功完成结构/数量/摘要检查后才写 `inputs_ready`；异常写 `failed` 并保留日志。中断留下 `preparing`，不自动恢复；重试使用新目录。`inputs_ready` 仅表示输入可用，不表示发现、配对或研究完成。

## 5. 实施与验证顺序

1. 落地本规划与共享类型、CSV 编解码、相机和清单，先运行纯 CPU 单元测试。
2. 接入准备配置和最小记录，运行解析/进程测试；所有读取/生成均在性能实验之外。
3. 落地摘要脚本、六行清单与完整准备流程，检查脚本失败路径和原目录不变。
4. 两后端完整构建与全量 CTest；按规范和本规划逐项核查并同步事实/审查和大规划。

| 验证输入 | 必须检查的结果 |
| --- | --- |
| 两地形 × 三预算，全部 64 点 | 共 384 行，所选子集 N×64；同地形预算间姿态/视图身份相同，协议首末点符合绝对坐标 |
| 冻结文件二次加载 | 全部 float 位模式及身份保持；输入行次序规范化，不依赖实际时间 |
| NO 投影近/远点 | NDC z 分别为 -1/1；不是 ZO 或应用默认裁剪面 |
| 词法/数值错误 | 引号内逗号/换行往返；未闭合引号、行宽、重复表头、UTF-8 错误、NaN/Inf、负数、溢出、尾字符拒绝 |
| 场景/资产错误 | 不合法组合/预算/投影/分组、错误实际尺寸/摘要、找不到资产或 ID 均失败 |
| 相机/目标错误 | 缺失/重复点、位置或哈希篡改、错误引用、根状态、上传阶段、重复目标/序号、空目标清单均失败 |
| 记录 | 字段数/值/转义一致，身份与线程分开，失败和不足状态保留，输出用途不可标成 formal |
| 准备命令 | 真正调用应用；进程返回值、输入数量、完整命令与哈希可独立重算；未提供目标明确标记 |
| 失败/重试 | 预先存在目录逐字节不变；缺行和坏摘要非零退出且 metadata 为 failed；中断状态不算完成 |
| 兼容 | 原 24 项 CTest 和新测试均注册运行；两个后端共享 CPU 实现，公共摘要使用 summary，正文按语义书写且通常不超过三行，标签不计入 |

## 6. 风险与取舍

Python 摘要边界是本阶段有意采用的最小追溯机制；裸 C++ 不提供完整可信运行身份，必须通过准备脚本进行 pilot 输入冻结。文件前后校验用于发现常规修改，不承诺对恶意并发写入的防护。每次后续 CPU 执行仍需重新核对输入摘要，不能只相信旧的 `inputs_ready`。

冻结相机要求同编译环境可重现；libm/编译器跨环境差异会显式校验失败，不能静默重新生成覆盖旧清单。完整跨环境身份与恢复后置。

本阶段没有新的核心架构待决项。采用用户已授权的自主实施方式；如果发现必须改变上位规划的模块依赖或算法/图形公共接口，再单独说明。

## 7. 实现情况

本阶段已于 2026-09-09 完成。实施与注释整改期间仅有开发授权，当时没有执行 `git add`、`git commit` 或 `git push`。用户随后明确许可提交，实现与注释整改记录为 `72f9c5f`，提交归档见第 7.6 节。

### 7.1 实际交付与核查

| 规划项 | 实际实现及结果 |
| --- | --- |
| 共享输入归属 | Types、Camera、Manifest、Records、Csv 在 `src/experiment/formal`；通用 CSV 在 `src/experiment`；无 DOD/App/图形 API 反向依赖 |
| 复用 | 公共 LOD 设置/阶段/FNV、显式 NO 视图构建和现有 HeightMap；未改公共算法、线程池或后端协议 |
| 场景输入 | 六行 `cpu-pilot-scenarios-v1.csv`，支持非空子集过滤；实际 129×129 与 547×547，固定协议 SHA-256 |
| 冻结相机 | A/NO、每场景 0..63，共 384 行；预算无关身份、首末坐标、裁剪面、9 位有效数字往返和单 ULP 篡改检查 |
| 目标引用 | 五阶段、1..63、唯一身份/序号、选择字段/分组及冻结相机引用；未提供目标显式 not_provided，已提供但未校验为 provided_unchecked |
| 最小记录 | 独立 pilot v1 与固定 exploratory；输入摘要实际接入，发现/配对写入器由测试验证，实际生产者留给 PREP-03/04 |
| 无窗口接入 | `cpu-pilot-inputs` 在普通场景创建前分流；纯解析器检查参数冲突；原配置继续通过现有回归 |
| 可追溯性 | Python hashlib；记录原/冻结/规范化清单、资产、可执行文件/DLL/构建缓存、包含未提交文件的源码摘要、实际命令和基本环境；前后重新核对 |
| 文件生命周期 | 脚本尝试目录和 C++ inputs 目录均独占创建；空目录也不复用；failed/preparing 与 inputs_ready 分开；没有自动恢复 |
| 架构收尾 | [代码事实](../../codebase/formal_experiment/cpu_pilot_input_contracts.md)与[审查记录](../../reviews/formal_experiment/prep_02_minimal_input_architecture_review.md)已同步；职责与依赖核查完成，后续注释专项整改见第 7.5 节 |

### 7.2 实际构建与测试

```powershell
cmake --preset relwithdebinfo-fetch -DPARALLEL_ROAM_BUILD_TESTS=ON
cmake --build --preset relwithdebinfo-fetch
ctest --test-dir build/relwithdebinfo-fetch -C RelWithDebInfo --output-on-failure

cmake --preset relwithdebinfo-d3d12-fetch -DPARALLEL_ROAM_BUILD_TESTS=ON
cmake --build --preset relwithdebinfo-d3d12-fetch
ctest --test-dir build/relwithdebinfo-d3d12-fetch -C RelWithDebInfo --output-on-failure
```

- OpenGL 完整应用构建成功；28/28 CTest，45.65 秒。
- D3D12 完整应用构建成功；28/28 CTest，41.83 秒。
- 两个构建目录分别保存 `prep02-configure.log`、`prep02-build.log`、`prep02-ctest.log`。当前构建日志未发现本阶段编译告警；配置沿用既有第三方 SDL 的弃用提示。
- 新增 `formal_experiment_manifest`、`formal_experiment_camera`、`formal_experiment_command_line`、`cpu_pilot_preparation`，原 24 项继续运行，包括注释覆盖门禁。三项 C++ 测试不链接图形库；Python 进程测试每次保留新尝试，输出在各自 `build/<preset>/tests/cpu-pilot-preparation/`。
- 真实进程覆盖六场景 384 点、单场景 64 点和五个合成目标；独立重算记录中的 SHA-256；修改 PGM 的单个像素字节会在启动应用前失败；63 行相机以失败退出；已有完整、空/中断语义由独占创建和保留文件检查验证。
- 两后端均通过 NO 投影测试，说明本 CPU 输入路径没有受 D3D12 默认深度约定影响；不代表 ZO 输入已经实现。

### 7.3 独立输入冻结产物

```powershell
python scripts/prepare_cpu_pilot.py --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe --build-preset relwithdebinfo-fetch --output-dir benchmark-output/prep-02/input-freeze-20260909
```

实际输出为六场景、384 个相机点，`run-metadata.json` 状态 `inputs_ready`、用途 exploratory；未提供目标，不生成空目标文件。目录包含原清单副本、规范化场景/相机、输入摘要、完整命令与日志、源码/二进制/资产/清单摘要和基本环境。

本机记录得到 Ryzen 9 9950X3D、16 核/32 逻辑线程、Normal 优先级、全 32 线程亲和性及平衡电源计划；应用报告 OpenGL、RelWithDebInfo、MSVC 193833145。以上为本次准备快照，不是正式运行准入或性能结论。

`benchmark-output/prep-02/` 通过本地 `.git/info/exclude` 排除；没有改仓库忽略规则，也没有把生成输入提交到 Git。

### 7.4 实施细化与剩余限制

没有改变总规划的模块、依赖或资源所有权。CSV 实现按小型输入整份读取后解析，不引入大型流式框架；源码/环境脚本采用已许可的 Python hashlib 方案。CSV 测试最初误将单个空字段当作语法错误，已纠正为验证空字段往返和真正的列数不足，清单必填校验仍严格保留。

审查时修正了“已提供但尚未验证目标”的状态表述，并将测试配置期望改为由 CMake 传入，避免把 RelWithDebInfo 验收写死为其他构建配置的唯一合法值。三项 C++ 测试只要求已有 GLM/STB，不以完整图形应用可用为注册前提。

裸 C++ 仅校验协议、尺寸和引用，必须配合准备脚本完成资产实际 SHA-256 核对。`inputs_ready` 只证明本次输入准备；后续运行前仍须核对当前文件。目标测试中的重放/特征身份为合成值，未进行 DOD 冻结状态验证。发现、计时配对、完整性统计、轨迹 B/C、ZO、上传、正式文件指纹与恢复仍按后续阶段实现。

### 7.5 注释专项整改

用户要求补齐 PREP-02 注释，范围为[专项审查](../../reviews/formal_experiment/prep_01_02_comment_compliance_review.md)的 C-01～C-03。实施前重读开发规范第 5.7 节、本规划、输入代码事实与审查，核对声明和实际分支。此次为既有实现的注释补充，不引入新架构决策。

全部选择 Extend，仅修改既有文件的说明：`ExperimentCsvCodec.cpp` 解释 CSV 状态、闭合引号和 EOF；`FormalExperimentManifest.h/.cpp` 说明清单加载契约、完整相机集合、根状态排除与选择序号连续性；`FormalExperimentCamera.h/.cpp` 说明生成范围、NO 视图及双重校验；`FormalExperimentRecords.h` 限定摘要成功状态；`FormalExperimentRunner.h` 说明目录前提、返回值和失败产物。各说明留在所属接口或逻辑旁，不新增文件、抽象、依赖或可执行行为，不处理 PREP-01 既有注释。

验收按上述问题逐项核对注释准确性，检查 summary 格式、正文长度、行尾标点和阶段编号；对修改前快照确认源码差异仅为整行注释，再运行注释覆盖率门禁。正文行数按第 7.7 节澄清的口径核查，标签不计入。纯注释变更无需重复两后端功能测试。整改结果完成后同步本节与两份相关审查。

实施结果：2026-09-09 完成上述七个源码文件的注释补充，C-01～C-03 已整改并逐项复核。对 `src/tests/scripts` 共 138 个文件的修改前快照检查，仅七个预定文件变化，去除整行注释后内容逐行一致；注释格式、连续行数、标点与阶段编号检查符合要求。覆盖率为 2550/16328 = 15.6%，各子模块门槛也满足。未改接口、依赖或可执行行为，未重复构建与功能测试；两份相关审查已同步，整改结束时尚未暂存或提交 Git。

### 7.6 授权提交归档

用户于 2026-09-09 要求“跟 prep-01 一样提交一下”，据此沿用代码与文档分开提交的方式。实现提交 `72f9c5f` 包含 23 个文件，覆盖共享输入、准备入口、脚本、场景清单、构建接入、测试及注释补充；阶段文档以独立 `update` 提交归档，包含本规划、总规划、代码事实、架构审查和注释专项记录。

提交信息仅描述修改内容和影响，构建、测试与检查结果保留在本规划和审查文档。提交范围不包含构建产物、实际冻结数据或个人配置；未执行远程推送。

### 7.7 逐文件语义复核

用户要求逐个阅读前两个准备阶段的文件，手工修正注释，不使用脚本替换；随后明确重点是自然标点与语义，并非统一改成两行。沿第 3 节文件清单和实现提交 `72f9c5f` 阅读现有实现、测试、脚本及构建文件，按当前实现判断注释是否准确。已有清楚的一行说明保持原样，复杂契约才按需要分行；summary 标签不计入正文三行限制。

本轮全部采用 Extend：场景和请求用途留在 `FormalExperimentTypes.h`，相机生成及视图重建契约留在 `FormalExperimentCamera.h`，清单加载范围与字段读取职责留在 `FormalExperimentManifest.h/.cpp`，准备状态留在 `FormalExperimentRecords.h`，目录和失败语义留在 `FormalExperimentRunner.h`。相邻 CSV 编解码、写出器、准备脚本、测试及构建文件逐个核对，已有说明准确时不强行修改。不新增文件、接口或依赖，不改变算法与运行行为。

完成后对照开发规范及本规划核查正文行数、标点和注释语义，确认无可执行代码差异并运行既有注释覆盖率检查；结果写入本节及两阶段注释专项审查。已有构建和功能测试结果保留为历史记录，不计作本轮重跑；本轮不提交 Git。

实施结果：23 个阶段文件已逐个阅读，其中 5 个与 PREP-01 重叠；另读共享摘要脚本以核对提取后的调用边界。按上述职责手工修正场景与请求用途、相机接口、清单加载与字段游标、准备状态和目录契约，并整理共享 CSV 中已有的发现说明。原本准确的单行摘要、流程说明及测试文件保留。当前全部 27 个修改的 C++ 文件（含 PREP-01 和此前 PREP-03 修改）去除整行注释后与 `570ecd0` 逐行一致，99 个摘要的正文均未超过三行，标点与标签核查完成；覆盖率 `src` 为 15.8%，各模块门槛满足，`git diff --check` 无问题。未改接口、依赖或执行逻辑，未重复构建和功能测试，未暂存或提交。逐文件处理及审查结论见[专项审查第 8 节](../../reviews/formal_experiment/prep_01_02_comment_compliance_review.md#8-逐文件阅读与语义复核)。
