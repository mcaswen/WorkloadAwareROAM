# PREP-01 / PREP-02 注释规范专项复核

> 日期：2026-09-09\
> 状态：逐文件语义复核完成；撤回 C-04 中将 summary 标签计入三行限制的判断；本轮修改未提交\
> 对象：PREP-01 实现提交 `bb1a89c`、PREP-02 实现提交 `72f9c5f`（复核时基于 `06cb24c` 的工作区），以及相关文件中保留的既有注释\
> 依据：[开发规范第 5.7 节](../../standards/development_guidelines.md#57-注释规范)、[审查指南](../review_guideline.md)、两个阶段小规划及用户指定的 `/// <summary>` 格式

## 1. 结论与口径

初次复核发现 PREP-02 的关键流程、摘要状态及接口契约说明不足，C-01～C-03 已按第 6 节整改。PREP-01 的行尾标点及部分语义问题也已处理，但 C-04 把 summary 标签计入正文三行限制的判断有误，现明确撤回，不能将当时压缩正文的做法继续作为合规依据。用户随后要求逐文件手工阅读和修改，最新结果见第 8 节；结论限定于这些阶段文件，不扩展为全项目注释审计。

此前审查偏重 `summary` 格式、无 `@brief` 和覆盖率门禁，未充分检查复杂流程的语义说明，也没有把相关文件中的既有问题明确列出。原审查中的“无未处理 Minor”不能继续用作完整注释合规结论；本专项记录补充并修正该结论，不撤销已有功能测试结果。

本次区分新增与继承问题，不把已有缺陷误记成 PREP-01 新增；也不把单文件低覆盖率、简单 getter 没有注释或每次字段赋值没有注释判成违规。第 5.7.18 条明确说明覆盖率不是逐文件目标。

## 2. 已满足的检查

- 初次扫描自有 `src/tests` 的 C++ 注释未发现 `@brief`。两个阶段新增公共结构体均有 `/// <summary>` 说明，包含用途或所在数据流；不要求正文加标签恰好三行。
- 初次扫描没有发现禁止的行尾标点或 PREP 编号；原来的物理行数判断已纠正，最新正文行数与标点核查见第 8 节。
- PREP-01 的数量解析注释说明了串行动作由调用方归一、自动线程上限与显式请求的差别；测试注释说明了受控并发夹具和借用线程池生命周期，具有实际语义。
- 初次执行 `tools/check_cpp_comment_coverage.ps1`：`src` 为 2517/16328 = 15.4%，满足 15%；DOD 为 20.1%，Classic 为 20.1%，render 为 12.2%，platform 为 20.0%，均满足脚本对应门槛。整改后的结果见第 6 节。
- 覆盖率脚本只统计行数，不检查注释准确性、连续行数、行尾标点或关键步骤是否有解释；因此通过脚本不能推出本专项全部合规。

## 3. 问题

### Critical

本次未发现由注释问题直接造成的严重功能错误；本次范围不包含重新验证算法正确性。

### Major

没有证据将本次注释问题定为模块边界或依赖设计层面的 Major。

### Minor

#### C-01：PREP-02 的复杂解析和引用校验缺少关键步骤说明

- 定位：[ExperimentCsvCodec.cpp 第 90 行](../../../src/experiment/ExperimentCsvCodec.cpp) 的 `ReadExperimentCsv`；[FormalExperimentManifest.cpp 第 269 行](../../../src/experiment/formal/FormalExperimentManifest.cpp) 的 `LoadTargetManifest`。
- 规范：第 5.7.12、5.7.16 条要求复杂逻辑按关键流程说明原理和约束。
- 实际：CSV 的 quoted/closed/started 状态、闭合引号之后为何禁止普通文本、EOF 如何区分尾部空字段与换行结束，没有相应说明。目标加载的完整相机集合约束、根状态 0 的排除原因、唯一序号结合首尾值证明连续性，也没有局部说明。
- 影响：维护者需要从布尔组合和数值判断反推输入规则，容易在修复边界时改变冻结输入语义。
- 建议：在状态定义、EOF 收尾、相机集合检查、目标根状态和序号连续性检查附近补短注释，解释约束来源与理由；不逐行解释赋值或每个 Require。
- 状态：已整改并复核。CSV 已解释状态互斥、空引号字段、转义、闭合后限制与 EOF；目标加载已解释完整相机集合、首次建立成本的排除、外部身份验证边界和连续序号判据。

#### C-02：PREP-02 的摘要注释没有限定成功状态

- 定位：[FormalExperimentRecords.h 第 13 行](../../../src/experiment/formal/FormalExperimentRecords.h) 的 `InputPreparationSummary`。
- 原文：“输入准备的完整性摘要仅证明结构就绪，不代表配对或外部摘要已验证”。
- 实际：类型默认 `Status=preparing`，运行器也用它写出 failed；只有 `inputs_validated` 才表示结构校验成功。
- 规范：第 5.7.3、5.7.14 条要求解释准确且与实际代码一致。
- 影响：将类型用途笼统表述为“证明结构就绪”，遗漏了必须先检查状态的条件。
- 建议：说明该类型记录准备状态与完整性，明确只有成功状态证明结构已校验，且不代表外部摘要或配对完成。
- 状态：已整改并复核。类型说明明确记录准备状态与完整性，只有 `inputs_validated` 表示结构校验成功，配对和外部摘要仍需另行验证。

#### C-03：PREP-02 的非显然公共接口条件未在声明处说明

- 定位：[FormalExperimentRunner.h 第 7 行](../../../src/benchmark/formal/FormalExperimentRunner.h)、[FormalExperimentManifest.h 第 15 行](../../../src/experiment/formal/FormalExperimentManifest.h)、[FormalExperimentCamera.h 第 7 行](../../../src/experiment/formal/FormalExperimentCamera.h)。行号定位对应整改后的 summary 起点。
- 实际：`PrepareCpuPilotInputs` 会创建目录并保留失败输出，要求目标目录不存在且父目录存在；清单加载以异常失败，并要求每个选中场景的完整相机集合；`GenerateCameraSample` 只支持 A 和 0..63。调用方单看声明不能得到这些条件。
- 规范判断：第 5.7.10 条不是要求给每个公共方法写注释，但这些接口恰好具有不显然的参数、失败和跨模块使用条件，适合补充契约说明。
- 建议：仅对上述边界补 summary，正文按语义书写；明显的 CSV 表头写入器、简单转换和 getter 不补套话。
- 状态：已补充并复核。声明处说明目录前提、成功/失败返回值、失败产物、完整相机集合和异常语义；相机接口说明生成范围、右手 NO 重建与哈希/轨迹校验边界。正文通常不超过三行，标签不计入。

#### C-04：PREP-01 的行尾标点问题及行数误判修正

- 撤回行数判断：`DataOrientedRoamMeshEmit.cpp:17/33/332`、`DataOrientedRoamParallel.h:39`、`DataOrientedRoamQueues.cpp:34` 的五处 summary，原先按包含标签的四个物理行判为超限。标签不属于正文，这些注释不能仅因四个物理行被判违规，也不需要为此记录例外。
- 整改前行尾标点：`TerrainLodBenchmark.cpp:264/265/266/658/664/665/1134/1660`，共八行，使用中文句号或逗号，不符合第 5.7.4 条。
- 归属：已用 `bb1a89c^` 对比确认上述注释在 PREP-01 前存在，不是此次新增。PREP-01 的新增/修改逻辑仍位于这些文件，因此不能笼统声称相关文件的注释已经全部规范化。
- 建议：清理禁止的行尾标点，按语义使用句内标点和必要的分行；不再要求合并正文。
- 状态：八行行尾标点已清理，“串行始终一个线程”遗漏零工作条件的表述已修正；物理行数误判已撤回。五处摘要已逐个重新阅读并按实际职责调整，详见第 8 节。

## 4. 补充观察

`TerrainLodBenchmarkCommandLine.cpp:221` 原先的“新下限与既有整数选项……”以修改时序描述差别。其后半句解释了两类范围语义，并非完全无意义，故不单凭“新”字认定违反第 5.7.1 条。本轮已改为直接说明只有 `--pass-*` 计数拒绝 `size_t` 最大值，与实际解析条件一致。

初次观察中，开发规范两个代码示例使用 `@brief`，与用户指定格式不一致；现在已同步为 summary。原先对示例物理行数的质疑一并撤回，当前规范明确只计算有效正文，并要求按语义使用句内标点，不统一扩写成两行。

## 5. 后续验收边界

整改应以补齐原因、条件和不变量为目标，不以堆注释达到百分比为目标。若只改注释，可检查注释格式、重新运行覆盖率门禁并核对无可执行代码差异，无需仅因此重复两后端全部功能测试。初次复核仅记录问题；后续整改结果单独记录如下。

## 6. PREP-02 整改与验证

2026-09-09 按用户“prep2 部分补上注释”实施。修改前重读开发规范、本阶段规划、代码事实及本专项审查，核对相关源码与协议中采样点 0 的初始化成本约束，并在小规划第 7.5 节记录文件归属与验证方式。

- 仅修改七个既有源码文件的注释：`ExperimentCsvCodec.cpp`、`FormalExperimentManifest.h/.cpp`、`FormalExperimentCamera.h/.cpp`、`FormalExperimentRecords.h`、`FormalExperimentRunner.h`。接口条件位于声明旁，关键不变量位于所属分支或校验旁。
- 对修改前 `src/tests/scripts` 共 138 个文件的快照核对，只有上述七个文件发生变化；去除整行 `//` / `///` 后逐行一致，未改可执行代码、接口签名、依赖或生命周期。
- 按 C-01～C-03 逐项对照实际条件与失败路径，补充注释和实现一致；当时七个文件的物理行数、行尾标点、`@brief` 和阶段编号扫描通过。物理行数不再作为正文限制的判据，最新核查采用第 8 节口径。
- 重新执行覆盖率脚本：`src` 为 2550/16328 = 15.6%；DOD 20.1%、Classic 20.1%、render 12.2%、platform 20.0%，均达到原门槛。覆盖率作为格式/语义复核的补充证据。
- 此次仅改注释和文档，未重复两后端功能测试；原构建及 CTest 结果仍保留为此前实现的验证记录，没有将其记作本次重跑。

本次无新增架构问题；C-01～C-03 已关闭，PREP-01 的既有 C-04 及规范示例不一致的观察仍保留。复核与整改期间未暂存或提交；用户随后许可按 PREP-01 方式提交，源码已归档为 `72f9c5f`，本记录随阶段文档单独提交，未推送。

## 7. PREP-01 注释优化与验证

2026-09-09 用户继续要求优化注释，按上一轮遗留的 PREP-01 问题处理。修改前读取开发规范、小规划、相关事实及审查，核对数量解析、阶段动作适配、网格写入、线程池同步和参数范围；文件归属及验收方案记入小规划第 8.3 节。

- 当时将五处四行 summary 压缩为三行；这基于错误的计数理解，不再列为合规改进。策略归一、槽位输出、全量重写及零工作条件的语义修正仍保留。
- 清理八行行尾标点及两处历史阶段编号、`C1` 标签；场景注释说明制造预算压力的目的，不以注释断言每帧观测结果。
- 补充空工作优先返回、零下限仍受工作量限制、网格下限使用实际脏槽位数量；线程池说明同步借用和捕获对象生命周期，无池路径明确只在调用线程遍历任务编号。
- 修改限定于 `DataOrientedRoamParallel.h`、`DataOrientedRoamQueues.cpp`、`DataOrientedRoamMeshEmit.cpp`、`TerrainLodBenchmark.cpp`、`TerrainLodBenchmarkCommandLine.cpp`，没有新增文件或改变职责、接口、依赖及执行行为。
- 与修改前 `db16eb6` 比较，五个源码文件去除整行注释后逐行一致；当时的格式和空白扫描已完成，其中 summary 行数口径由第 8 节修正。
- 覆盖率：`src` 为 2548/16328 = 15.6%；DOD 为 937/4677，满足 20% 门槛；Classic 20.1%、render 12.2%、platform 20.0% 也满足各自门槛。未因纯注释修改重复构建和功能测试。

C-04 中有效的标点与语义问题，以及第 4 节的修改时序表述已处理，无新增架构问题。本节为当时的整改记录；后续计数口径与规范示例修正见第 8 节。

## 8. 逐文件阅读与语义复核

2026-09-09 按用户要求，逐个阅读 PREP-01 的 17 个文件和 PREP-02 的 23 个文件，重叠文件只读一遍，共 35 个实现、测试、脚本、清单与构建文件；另读共享脚本 `cpu_pilot_support.py`，核对准备脚本提取后的职责。修改前已阅读开发规范、阶段规划、相关代码事实和审查；文件归属写入 PREP-01 第 8.4 节、PREP-02 第 7.7 节。所有改写由手工补丁完成，没有使用脚本替换注释。

### 文件核对结果

下表路径按所属目录简写；“保留”表示已核对现有说明和实现，不为凑行数修改。

| 文件 | 阅读与处理结果 |
| --- | --- |
| `CMakeLists.txt`、`tests/CMakeLists.txt` | 核对源码注册、测试依赖和参数传递，保留现有说明 |
| `benchmark/TerrainLodBenchmark.h/.cpp` | 修正任务分流、共同输入、计时边界、统计口径及场景目的；正文按语义采用一行或多行 |
| `benchmark/TerrainLodBenchmarkCommandLine.h/.cpp` | 说明解析结果与执行的边界、整数完整解析及参数范围；将预算重入说明移到对应分支 |
| `algorithms/TerrainLodPassTrace.h`、`algorithms/ITerrainLodAlgorithm.h` | 区分线程请求和实际执行；明确配置哈希及借用资源检查的能力边界 |
| `data_oriented_roam/DataOrientedRoamParallel.h`、`DataOrientedRoamQueues.cpp`、`DataOrientedRoamMeshEmit.cpp` | 核对任务数量、空工作、同步生命周期和槽位写入，分别手工调整摘要 |
| `experiment/TerrainLodExperimentCsv.h/.cpp` | 头文件明确最终设置与分隔符职责；实现的字段顺序说明保留 |
| `tests/TerrainLodBenchmarkCommandLineTests.cpp`、`DataOrientedRoamPassPolicyTests.cpp`、`TerrainLodBenchmarkPolicyTests.cmake`、`TerrainLodExperimentCsvTests.cpp` | 核对参数边界、受控线程夹具、借用生命周期及 CSV 断言，保留原文件 |
| `experiment/ExperimentCsvCodec.h/.cpp` | 核对引号状态、EOF、UTF-8、数值解析和往返精度，保留已有说明 |
| `experiment/formal/FormalExperimentTypes.h` | 明确场景参数、资产声明及清单驱动请求用途；保留清楚的相机和目标单行摘要 |
| `experiment/formal/FormalExperimentCamera.h/.cpp` | 头文件按生成约束、失败语义和身份校验边界调整标点与分行；实现说明保留 |
| `experiment/formal/FormalExperimentManifest.h/.cpp` | 声明处区分读取、结构校验与外部摘要验证；字段游标说明与其版本/用途检查保持一致 |
| `experiment/formal/FormalExperimentRecords.h`、`FormalExperimentCsv.h/.cpp` | 明确准备成功状态的限定范围，保留完整配对记录语义；发现摘要与 CSV 契约使用自然标点，写出逻辑未改 |
| `benchmark/formal/FormalExperimentRunner.h/.cpp` | 头文件说明独占目录、返回值及失败产物；实现的目录所有权和输出重读说明保留 |
| `docs/parallel-roam/cpu-pilot-scenarios-v1.csv`、`scripts/prepare_cpu_pilot.py` | 核对协议清单与脚本的输入、摘要和失败处理，保留原文件 |
| `tests/ExperimentCameraTests.cpp`、`ExperimentManifestTests.cpp`、`FormalExperimentCommandLineTests.cpp`、`test_cpu_pilot_preparation.py` | 核对独立协议坐标、篡改拒绝、参数互斥、摘要和目录保护，保留原文件 |

同步整理此前未提交的 PREP-03 注释：例如容器哈希改为“按顺序编码容器的长度与有效元素，排除预留容量和内存地址”，一行即可完整表达；生命周期或验证边界需要分别交代时保留多行。正文三行是上限约束，不是排版目标。

### 核查与结论

- **Critical**：无新增问题。当前全部 27 个修改的 C++ 文件（含此前 PREP-03 修改）与 `570ecd0` 对比，去除整行注释后逐行一致，未改变执行逻辑、签名或数据布局。
- **Major**：无新增问题。全部说明仍位于拥有相应职责的文件；未新增抽象、移动职责、改变依赖或资源所有权。
- **Minor**：计数口径误判已撤回，句内标点、摘要成功状态、输入身份和线程语义已修正。27 个文件的 99 个摘要均闭合，正文为一行或两行，均未超过三行；未发现禁止的行尾标点，新增行无 `@brief` 或 PREP 编号。

只读脚本用于比对与格式核查，不写入文件；`git diff --check` 无问题。现有覆盖率脚本结果为 `src` 2722/17178 = 15.8%、DOD 1032/5002 = 20.6%、Classic 20.1%、render 12.2%、platform 20.0%，均满足对应阈值。覆盖率只作辅助证据，语义由逐文件阅读核对。本轮没有重复构建和功能测试，原有实验产物及测试证据保留为历史记录，修改未暂存或提交。
