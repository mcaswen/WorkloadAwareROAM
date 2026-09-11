# CPU-CBT-01：持续串行批量更新小规划

> 类型：Minor Plan，落实已通过大规划的第一阶段
> 状态：用户已确认实施；串行更新、验证、性能对照和阶段核查已完成
> 日期：2026-09-12
> 上位规划：[最小 CPU 类 CBT 批量更新](cpu_cbt_minimal_implementation_plan.md)
> 起点：WorkloadAwareROAM `b078d59`；RoamTesting `d462089d5c2888cc09537a6a3f44a7c33cbf4499`

## 1. 目标、范围和前置核对

本阶段交付一个从六个基础三角形开始，能持续执行分类、联合细分计划、细分、合并、释放复用和 CPU 网格输出的串行基线。两个自然输入各执行固定 96 轮，报告硬预算、工作量、适应过程与新增 CPU 成本，为第二阶段的原参考串行档提供可追溯起点。

已阅读[开发规范](../../standards/development_guidelines.md)、[规划规范](../plan_guideline.md)、[审查规范](../../reviews/review_guideline.md)、上位规划及用户评审，复核参考七组 C++ 文件、分类与合并夹具、参考宿主更新入口、当前构建、地形/视图类型和既有性能探针。

本阶段所有更新均串行，直接使用参考提交函数。CPU-CBT-02 的并行内核与线程池适配留待下一阶段；不引入质量恢复或选择器优化。即使固定物理顺序在紧预算下分配细节不佳，也保留并报告，不修正为 ROAM 分数、全局 top-k 或质量感知分配。

本小规划先冻结两项 Review 前提：**Peking 恢复 `TerrainSize=80、HeightScale=12`；采用有参考宿主阶段顺序依据的 `reference-primitive-driven CPU round semantics`，不宣称完整复现 CBT-2024。**

## 2. 一轮更新的语义来源

参考仓库确有完整的宿主调度：[D3D12CbtFramePipeline.cpp](../../../third_party/RoamTesting/src/algorithms/cbt_2024/d3d12/D3D12CbtFramePipeline.cpp) 的 `RecordFrame`。此次只读核对其阶段关系，不编译、运行或迁移图形路径。纯 C++ 文件提供阶段函数，当前没有供本项目直接调用的持久 CPU 更新循环。

| 本轮阶段 | 已核实参考依据 | CPU 原型的关系与新增选择 |
| --- | --- | --- |
| 重置与分类 | `RecordFrame` 的 `Reset → Classify`；`CbtTopologyE0.hlsl::CSClassify` 从上一轮活动叶取得细分和合并候选 | 复用 C++ 分类公式；本项目按物理槽位递增收集。重算全部活动叶几何，不迁移参考几何缓存或原子追加顺序 |
| 联合计划与分配 | `RecordFrame` 的 `Split → Allocate → NeighborCopy`，分配使用旧占用树 | 复用 `PlanCbtSplits`、`AllocateCbtSplitSlots`；CPU 增加索引适配与 `min(空闲槽, B-N)` 硬预算 |
| 细分及传播 | `RecordFrame` 的 `Bisect → PropagateBisect` | 直接调用 `CommitCbtBisects`，取得其完整顺序结果 |
| 旧合并候选重验 | `RecordFrame` 在细分传播后运行 `PrepareSimplify`；`CSPrepareSimplifyF` 读取原分类列表，在新拓扑上重新检查状态、同层兄弟及相对邻接 | 用原分类得到的物理槽列表调用 `CommitCbtSimplifications`；其内部顺序为 `PrepareSimplification → CommitSimplification → PropagateSimplification` |
| 归约与发布 | `RecordFrame` 在两类提交和传播结束后执行三段归约，再建立索引 | CPU 顺序应用最终占用变化、调用 `Reduce`、重建活动索引并移动发布；与参考中途更新位域、最后归约的内部表示时机不同 |
| 输出几何 | 参考 `CbtTerrainGeometry` 解码子三角形与父级辅助位置 | 包装为全量 `TerrainMeshData` 输出，不实现参考绘制、间接命令或增量几何路径 |

因此，**分类先于细分、旧合并候选在细分后重验、两类操作后归约**有明确参考依据；确定性候选顺序、CPU 新旧代所有权、槽位适配、硬预算和发布失败语义由本项目定义。CPU 结果须由定向夹具和持续轨迹验证，不能用“primitive 来自参考”代替组合正确性证据。

源级核对还发现一个直接影响接入的约束：`PlanCbtSplits` 检查输入数组中的每个节点都具有非零 `HeapId`，使用数组内的局部邻接编号；不能把带有空闲槽的完整物理池直接传入。第 4 节冻结活动叶紧凑视图与结果回填，属于既定包装职责，不修改参考接口。

## 3. 自然输入冻结

PREP-02 的 `benchmark-output/prep-02/input-freeze-20260909/inputs/scenarios.csv` 已确认 Peking 为 `80/12`、test129 为 `30/4`。上位规划的统一 `30/4` 已纠正；尚未按误写参数运行自然实验。

| 新原型场景编号 | 资产及真实尺寸 | `TerrainSize` / `HeightScale` | `B` | 资产 SHA-256 |
| --- | --- | --- | --- | --- |
| `cpu-cbt-test129-b4096-v1` | `Hm_Terrain_Test_129.pgm`，129×129 | 30 / 4 | 4096 | `88e7688c5ee298e4df16a250ae37e82c1e48ae8e66c973facfcc0eb208d6448c` |
| `cpu-cbt-peking547-b20000-v1` | `Hm_Terrain_Peking_513.png`，547×547 | 80 / 12 | 20000 | `1b084bacf08e3cb67afc3744d98cc80c6dfb7d55280d93d02a3f93294e121031` |

共同参数：动态槽容量 131072，六个永久基础槽，`MaxHeapBitDepth=20`，基础位长 4，面积阈值 50 像素平方，分辨率 1280×720。资产加载后再次核查尺寸与散列，不能仅凭文件名接受不同输入。

相机固定为 `position=(0,h,0)`、`target=(0,0,0)`、`up=(0,0,-1)`、`forward=(0,-1,0)`。`view=glm::lookAtRH`；`projection=glm::perspectiveRH_NO(radians(60),1280/720,0.05,1000)`；`BuildTerrainLodViewInput` 的 `usesZeroToOneDepth=false`。浮点矩阵只生成一次并原样用于各轮，记录实际矩阵值与散列。

轮序号采用 0～95：0～31 的 `h=20`，32～63 的 `h=80`，64～95 的 `h=20`。每个进程从相同六基础状态开始，每段严格执行 32 轮，不因稳定提前停止。相机切换也不重置拓扑。

这是**CPU-CBT 专用轨迹**。只沿用历史资产和尺度，不沿用 `peking547-a-b20000` 的相机、分类、初始状态或更新策略，不能直接对比 P5/GATE 的历史性能或行为。自然输入不用于验收“所有分支必须出现”，也不为预算竞争或更大批次调整阈值、预算、相机和轮数。

## 4. 文件归属、接口与实现步骤

### 4.1 文件和依赖

| 选择 / 文件 | 本阶段职责 |
| --- | --- |
| Create：`src/algorithms/cpu_cbt/CpuCbtState.h/.cpp` | 设置、持久状态、六基础初始化；初始化空闲项、完整物理池及活动索引。只拥有数据和状态生命周期 |
| Wrap：`CpuCbtUpdate.h/.cpp` | 活动视图适配、固定分类状态映射、联合计划、硬预算、串行提交与发布；定义 `CpuCbtUpdateReport` 及少量运行选项。索引转换为本文件私有 helper，不新建通用图映射层 |
| Wrap：`CpuCbtMesh.h/.cpp` | 复用 `EvaluateCbtTerrainGeometry` 提供分类四点与完整网格，按活动顺序写入三角形独立顶点，不做顶点去重和脏区缓存 |
| Create：`tests/CpuCbtTests.cpp` | 解析夹具、独立不变量、轮次组合语义、失败不发布及确定性核对；测试专属紧凑池映射只在夹具中使用 |
| Create：`tests/CpuCbtProbe.cpp` | 拥有两个固定轨迹、诊断/计时模式、CSV 与段摘要；本阶段仅串行，不建线程池 |
| Extend：`cmake/ProjectOptions.cmake`、`tests/CMakeLists.txt` | 默认关闭开关、纯 C++ 参考库和两个独立目标。原型打开但 GLM/STB 或参考文件缺失时明确失败，不能静默跳过目标 |
| Reuse | 七组参考 `.cpp`、两个 ABI 常量头、当前 `HeightMap`、`TerrainLodViewInput`、`TerrainMeshData`、计时与已有版本对照工具 |

依赖保持 `驱动 → CPU CBT → 纯 C++ 参考/公共地形类型`。不引入 DOD 状态依赖，不移动公共接口，不修改参考仓库。包含路径优先当前 `src/`，再到参考 `src/`；不让旧 `ITerrainLodAlgorithm.h` 遮蔽当前头文件。新增自有代码遵守中文自然注释和 `/// <summary>` 规范，不批量清理参考代码历史注释。

核心入口沿用上位规划的 `UpdateCpuCbt` 和 `BuildCpuCbtMesh`。本阶段 `UpdateCpuCbt` 同步串行完成全部工作；状态由调用者独占持有，输入高度图只读。运行选项只控制第 5 节的诊断与计时，不参与分类、排序、预算或停止决策；本阶段不预建并行内核。

### 4.2 实施顺序

1. **保存前置身份和性能依据。** 冻结当前源码、参考子集、编译器及旧生产探针，按第 7 节取得关闭状态基线；然后添加可选目标与六基础状态。
2. **准备几何与固定分类。** 在本轮临时数据中重置模板、分配项、问题邻接和传播状态。分类填写细分/不变/合并状态，可见性只作元数据；非基础负分类均可进入合并状态，只有偶数逻辑编号发出合并根请求。
3. **构造紧凑计划视图。** 活动物理槽升序形成 `localToPhysical`，建立逆映射，转换三邻接与候选列表。无效边界保留哨兵，非边界邻居若不是活动槽则报错。按参考函数要求只传活动节点。
4. **形成并回填计划。** 在该视图执行 `PlanCbtSplits`，额度为 `min(oldFreeDynamicSlots,B-N_before)`；按紧凑模板顺序调用 `AllocateCbtSplitSlots`。计划返回的节点位置是局部索引，但分配出来的新槽已经是物理索引：仅将模板所属节点和模板列表映回物理槽，不能再转换新槽编号。回填完整物理节点数据后调用顺序参考提交。
5. **重验合并与发布。** 将最初的物理合并列表传给细分后的 `CommitCbtSimplifications`。检查两次结果和数量后，在临时占用状态中应用细分置位、合并清位，再 `Reduce`、重建活动索引；最后一次性发布。六基础槽不得释放；任何失败保持上一轮已发布状态。
6. **完成网格和诊断。** 每轮生成全量 CPU 网格，输出计数和计时；诊断模式在计时之外做第 5、6 节检查和段摘要。写入代码事实、阶段审查并对照本规划逐项核查。

每轮必须满足 `N_before=6+oldOccupancy`、`N_split=N_before+acceptedSlots≤B`、`N_after=N_split-releasedSlots`、`N_after=6+newOccupancy`。`RequiredSlotCount`、唯一分配数和实际提交新增数一致；合并不能用来掩盖细分阶段超预算。

## 5. 必需记录与诊断口径

### 5.1 预算竞争

| 字段 | 精确定义与用途 |
| --- | --- |
| `splitProposalCount` | 本轮开始分类得到的根细分请求数，尚未由模板合并去重 |
| `poolOnlyRequiredSlots` | 仅在诊断模式，以同一冻结紧凑视图、同一候选顺序和 `oldFreeDynamicSlots` 额度额外调用一次参考计划器，取得其 `RequiredSlotCount`；用于观测去掉三角形额度限制后的计划需求，不分配、不提交、不改变真实状态 |
| `poolOnlyReservationRejections` | 上述诊断计划的预留拒绝数；非零时其需求仍受物理容量限制，不能称为无约束总需求或理论最小闭包成本 |
| `requiredSlots` / `acceptedSlots` | 真实带硬预算计划的 `RequiredSlotCount` / 实际成功提交的新动态槽数；成功轮必须相等，失败单独标记，不输出伪造的成功行 |
| `budgetRemainingBefore` / `budgetRemainingAfterSplit` / `budgetRemainingAfter` | 分别为 `B-N_before`、`B-N_split`、`B-N_after`，与计划器的保守预留余额分开记录 |
| `reservationRejectedCount` / `plannerRemainingSlots` | 原计划器的预留拒绝数 / `RemainingMemory`，不能直接解释为精确剩余三角形数 |
| `budgetLimitedRound` | 实际预留拒绝数非零，且 `B-N_before < oldFreeDynamicSlots`。本轮额度受三角形预算限制；相等时标为预算/物理容量共同限制，不强行归因 |
| `activeTriangleCount` / `budgetUtilization` | 整轮发布后的实际数量与 `N_after/B`；另保存 `N_before`、`N_split` 和旧空闲槽数 |

这个额外计划仅是**冻结状态上的预算诊断**，不执行另一条轨迹，也不参与选择。它只在每个自然输入的一次诊断进程中计算，计时进程关闭，空字段记为未采集而非 0；真实输入、工作量和输出摘要匹配后按场景/轮序关联诊断与计时记录。它复用已有函数，不增加闭包追踪、质量排序或新选择器。

每个自然输入汇总预算受限轮数、总预留拒绝数、各段及最终利用率，并同时展示根请求数、需求与接受数。预留不足可以源于保守上界，不自动等于“实际闭包绝对放不下”。若未观测到自然预算竞争，明确报告：**硬预算实现已由夹具验证，但自然原型轨迹未覆盖有意义的预算竞争。** 不重新调参制造竞争。

### 5.2 收敛与相机变化后的适应

每轮记录 `splitTemplateCount`、四模板分布、`pairMergeCount`、`quadMergeCount`、`simplifyCount`、`releasedSlots` 和 `occupancyDelta`。其中 `simplifyCount=pairMergeCount+quadMergeCount` 是合并任务数，`releasedSlots` 是实际数量，二者不混用。`occupancyDelta=acceptedSlots-releasedSlots` 为净变化，不能用净变化 0 代替无拓扑修改。

每段固定视图定义一次“无拓扑修改轮”为 `splitTemplateCount=0 && simplifyCount=0`，合法性诊断同时核对逻辑拓扑未变。段摘要包含：

- `firstStableRound`：本段首次无拓扑修改的局部轮号，范围 1～32；只是观测诊断，不保证后续稳定。
- `adaptationLatencyRounds`：截至段尾持续无拓扑修改的后缀起点，要求该后缀至少包含两轮；从本段第一轮计数。没有这样的后缀时为空，记录 `not_stable_within_32_rounds`，不能填成 32 或追加轮数。
- `adaptationUpdateCpuMs`：从本段开始至上述起点累计的实际更新时间。相机切换发生在全局轮 32、64；首段称为初始细化延迟，后两段称为相机变化后的适应延迟。
- `pendingSplitProposalsAtStableStart`、预算拒绝数和利用率：区分无需求的静止与预算限制下的停滞。稳定只表示本基线在该输入下不再修改拓扑，不代表几何质量达标或获得了最优网格。

收敛摘要由固定轮序数据计算，不控制执行。计时报告同时保留每轮成本和达到观测稳定所需的轮数/累计成本，避免以“单轮便宜”掩盖多轮适应代价。

## 6. 最小正确性验证

仅新增一个 CTest `CpuCbt`，其内部有以下定向用例；不运行参考仓库全部测试或项目全部 CTest。

| 组合 | 覆盖的具体风险 |
| --- | --- |
| 六基础初始化、稀疏物理槽与紧凑映射往返 | 永久槽与动态槽混淆；把新分配的物理编号错误当成局部编号；空闲节点传入计划器 |
| 参考边界/面对面/共享链夹具及四模板 | 联合模板与分配正确；重复闭包不重复提交；预留拒绝不留下部分模板 |
| 参考两节点/四节点合并及连续细分—合并—再分配 | 合并重新验证、邻接传播、永久槽保护和释放复用 |
| 同轮细分使原合并候选失效，另含一个仍有效的旧合并请求 | 必须消费轮开始列表并在新代重验，不能提前合并、漏掉重验或重新分类整轮 |
| `B=6`、边界额度、共享请求竞争、空闲槽不足、非法编号/深度 | 硬预算独立于槽池；细分后即不能超限；失败不发布 |
| 固定诊断/计时选项切换 | 额外需求计划和全局核查不改变分类、模板、预算、拓扑或网格 |

纯模板夹具可沿用参考测试的小尺寸映射。用于实际 `CbtOccupancyTree` 秩选择的夹具使用其支持的 128K 容量，不伪造不受支持的容量枚举。自然轨迹只验证实际覆盖；缺少某种操作时由上述解析夹具承担覆盖，不修改自然参数。

完整几何检查放在测试/诊断驱动：活动 `heapID` 唯一且无祖先重叠、编号属于六个基础前缀；邻接范围、活动性和反向引用有效；解码三角形非退化，内部完整边成对，开放边位于单位域边界，UV 总面积覆盖单位方形。拓扑与预算整数关系必须精确成立。

UV 坐标比较使用 `1e-6` 绝对容差，总面积使用 `1e-5` 绝对容差；二者均小于本轮位长 20 对应的最细边尺度。重心和面积累加用 double。端点配对须检查原始距离，不仅比较量化哈希；歧义匹配、退化或裂缝不能靠放宽容差解决，保存反例后 Review。参考夹具对照与独立不变量同时保留。

每个自然场景只运行一次 `--mode validate`，覆盖全部 96 轮的发布状态和网格；计时进程按相同轮序核对工作量及输出摘要，不重复运行昂贵全局几何检查。`CpuCbt` 已覆盖的解析函数不另外再跑一遍嵌套仓库测试。

## 7. 构建、运行与性能协议

以下为实施使用的命令与协议，实际结果见文末。所有命令在项目根目录执行；原型输出目录必须新建且拒绝覆盖。

### 7.1 关闭状态基线与可选构建

修改前，配置独立构建目录，仅构建和运行无窗口旧探针，保存新功能关闭的基线。原有 CMake 在 `APP=ON` 时才准备 GLM/STB，`APP=OFF` 会跳过所需旧探针目标，因此基线配置使用 `APP=ON`，不构建或运行图形应用；当时尚无新开关，无需传入它：

```powershell
cmake -S . -B build/cpu-cbt-01 -G "Visual Studio 17 2022" -A x64 -DPARALLEL_ROAM_BUILD_APP=ON -DPARALLEL_ROAM_BUILD_TESTS=ON -DPARALLEL_ROAM_FETCH_MISSING_DEPS=OFF
cmake --build build/cpu-cbt-01 --config Release --target parallel_roam_runtime_performance_probe --parallel 8
```

保存旧程序到 `benchmark-output/cpu-cbt/cpu-cbt-01/before/`，同时保存源码、参考子集文件身份、测试驱动及 CMakeCache。源码身份必须包含根仓库未跟踪的新文件和嵌套依赖的实际编译文件；不得只用 `git diff` 或主仓库提交替代。

实现后先保持新开关默认 OFF、相同 `APP=ON` 配置重建上述旧探针，保存到 `after-disabled/`，确认未引入参考依赖。新增原型块在 `tests/CMakeLists.txt` 内复用既有 GLM/STB 准备函数，因此原型可以真正关闭应用构建；随后显式开启：

```powershell
cmake -S . -B build/cpu-cbt-01 -DPARALLEL_ROAM_BUILD_APP=OFF -DPARALLEL_ROAM_BUILD_CPU_CBT_PROTOTYPE=ON
cmake --build build/cpu-cbt-01 --config Release --target parallel_roam_cpu_cbt_tests parallel_roam_cpu_cbt_probe --parallel 8
ctest --test-dir build/cpu-cbt-01 -C Release -R '^CpuCbt$' --output-on-failure
```

只构建所列目标，沿用本地 GLM/STB，不拉取新依赖。`CpuCbt` 的工作目录固定到项目根，超时 120 秒；自然诊断与计时进程每项上限同为 120 秒。超时保留证据，停止该项，不自动扩大资源上限。

### 7.2 原型命令、产物与诊断集合

```powershell
build/cpu-cbt-01/tests/Release/parallel_roam_cpu_cbt_probe.exe --scenario cpu-cbt-test129-b4096-v1 --mode validate --output benchmark-output/cpu-cbt/cpu-cbt-01/validation/test129
build/cpu-cbt-01/tests/Release/parallel_roam_cpu_cbt_probe.exe --scenario cpu-cbt-peking547-b20000-v1 --mode validate --output benchmark-output/cpu-cbt/cpu-cbt-01/validation/peking547
build/cpu-cbt-01/tests/Release/parallel_roam_cpu_cbt_probe.exe --scenario cpu-cbt-test129-b4096-v1 --mode measure --output benchmark-output/cpu-cbt/cpu-cbt-01/measurement/test129/run-1
```

最后一条为单次测量命令模板；另一个场景只替换固定场景编号与输出目录。本阶段不接受自定义面积阈值、质量策略、轮数或相机参数。输出包含 `rounds.csv`、`segments.csv` 和设置/来源元数据；原型驱动用普通流写出，复用现有 CSV 转义能力，不扩展正式实验 schema。阶段中文报告由 Agent 汇总到 `docs/reviews/cpu_cbt/`。

| 项目 | `validate` | `measure` |
| --- | --- | --- |
| 参数、索引映射、预算与提交必要检查 | 开 | 开，保持相同算法行为 |
| 仅受物理池限制的诊断计划 | 开，每轮一次 | 关，字段标为未采集 |
| 全局拓扑/UV 几何审计 | 开 | 关 |
| 基础工作计数和分段墙钟计时 | 开，但不作为性能结果 | 开 |
| 网格/工作量摘要及 CSV 写入 | 更新和网格计时外 | 更新和网格计时外 |
| 质量评价、逐节点日志、线程身份采集 | 关 | 关 |

诊断选项不变性由 `CpuCbt` 中的解析组合用例核对，不在自然轨迹中为此再执行第二条更新路径。

计时包含完整分类几何、候选收集、紧凑映射、计划/分配/回填、数组复制、细分提交及传播、合并及传播、占用归约/活动索引；另计全量网格输出与 `update+mesh` 外层总耗时。第一阶段不修改参考函数插入内部计时，所以细分填写/传播、合并准备/提交/传播分别先以组合调用成本报告，未分解部分记为未分解，不能用两次测量相减冒充内部阶段耗时。第二阶段再对新内核直接分段。

### 7.3 前后比对与新增成本

原型关闭的旧路径只复用现有 `test129-a-b4096 / serial-incremental / diagnostics-off` 一组探针配置，采用 PREP-02 已冻结的场景和相机清单。三个开关固定为 `EnablePassEvidence=false`、`EnableTopologyValidation=false`、`EnableTopologyPairEvidence=false`；探针外围结果校验保留，不纳入算法计时。

使用 `scripts/compare_runtime_performance.py`：把既有 `gate-03-close-20260912/probe-config.json` 的单组配置复制到本阶段产物目录，保持输入、组、模式不变，`versions.old/new` 分别指向本阶段 `before/after-disabled/` 的程序、源码包、Cache 和相同驱动。采集命令固定为：

```powershell
python scripts/compare_runtime_performance.py --config benchmark-output/cpu-cbt/cpu-cbt-01/probe-config.json --output benchmark-output/cpu-cbt/cpu-cbt-01/comparison --cohort cpu-cbt-01-disabled
```

该工具执行 1 个预热块、5 个独立测量块及 AB/BA 交错。沿用其 12 项整体/拓扑中位数与 P95 指标，并核对结果和工作量；历史版本的程序或报告只有同环境且身份有效时才复用，否则采用上述当前前置版本，不沿用不同环境下的数字。

新增 CPU CBT 原型没有功能等价的前版本，不计算相对 ROAM 或“零成本”的加速。每个场景单独预热 1 个进程、测量 5 个进程，每个进程固定 96 轮；两个场景按预注册编号顺序执行。保存进程内每段/全轨迹中位数和 P95、进程间波动、适应轮数及累计成本，作为 CPU-CBT-02 原参考串行档的基线。新原型的 12 个进程与旧路径对照顺序执行，不同时构建或测量；无需新运行框架。

独立进程为主要统计单位，不将 96 轮视为 96 个独立样本。初始化、加载、线程准备及输出 I/O 单列，不能藏入或移出版本间不同的计时边界。采集前记录硬件、编译器、优化选项、源码/程序散列、输入身份和完整命令。

工程退化仍按开发规范的 `max(0.05 ms, 原基线5%, 原基线五次极差)` 门槛与至多一次完整复测处理。确认有实际影响的问题时，先在 `docs/reviews/cpu_cbt/` 独立注明分析过程、已证实/未证实原因，再由用户决定修复规划；不追几微秒差异，不增加无关测试。

## 8. 完成条件、停止条件与后续边界

完成需同时满足：两项冻结契约落实；串行更新及网格输出可持续运行；硬预算和组合语义由定向夹具验证；两个自然轨迹各有完整 96 轮记录；预算竞争有或没有都明确报告；收敛/停滞不混同质量；前后性能对照与新增成本归档；代码事实和阶段审查与实际实现一致。

若参考函数无法组成合法持续状态，或适配需要改写参考模板、合并条件或图形依赖，保存反例后停止并 Review。诊断所需少量包装不能扩大为通用追踪平台。自然输入缺少预算竞争或 32 轮内未稳定不自动判实现失败，也不以此扩展实验；正确性失败和性能未验证则不能标完成。

本阶段不产生并行加速结论。第二阶段保留 **reference serial / new serial / new parallel** 三档：前两者区分移植成本，后两者区分线程收益；分别报告模板填写、完整拓扑更新、更新加网格输出的实测比值。全局计划、外部传播、合并和占用归约仍串行。若完整更新收益及可利用并行部分有限，先提交串行瓶颈证据 Review，不自动开展质量恢复研究。

### 实现结果与核查

2026-09-12 完成，实际文件与第 4.1 节一致：三个核心头/源文件对、两个驱动及两个构建入口；没有新增线程池、细分内核、质量策略或运行框架。Peking 固定 `80/12`，顺序保持参考 primitive 驱动的 CPU 轮次语义。参考仓库源码未修改。

`CpuCbt` 定向 CTest 通过（1/1，0.31 秒），覆盖四模板、共享闭包、保守预留、两类合并、同轮旧合并重验、释放复用、硬预算、诊断不变性和失败不发布。两个自然场景各一次诊断，共 192 轮通过编号、邻接、完整边和 UV 覆盖检查。每场景另有一次进程预热及五次独立计量，逐轮工作量、拓扑与网格摘要均与诊断记录一致。

| 自然场景 | 预算受限轮数 / 96 | 累计预留拒绝数 | 最终数量 / 预算 | 三段适应轮数 |
| --- | --- | --- | --- | --- |
| test129 | 55 | 157119 | 4096 / 4096 | 11 / 4 / 4 |
| Peking | 48 | 401739 | 19999 / 20000 | 16 / 10 / 10 |

拒绝总数包含同一请求跨轮重复出现；近景稳定尾部仍有大量待细分请求，属于预算下的停滞，不能称为质量收敛。四模板和两类合并在自然轨迹中均有出现；参数未为覆盖而调整。

新增原型完整更新时间的进程级中位数再取跨进程中位数为 test129 **4.11235 ms**、Peking **10.18965 ms**；`update+mesh` 分别为 **5.38235 ms**、**16.88055 ms**。这些是串行成本，不是相对 ROAM 或多核的加速。

关闭状态旧探针的前后程序 SHA-256 相同。首轮两个整体中位数指标超过修改前冻结门槛，按规范完成唯一一次完整复测后均未重现，结论为**无版本相关退化证据**。12 项指标、原始门槛和两批配对差全部保留，不用同期扩大后的极差替代原门槛，也不继续追加采样。

唯一构建调整已反映在第 7.1 节：旧基线准备依赖需 `APP=ON`，新原型在授权的测试构建入口自行准备纯 CPU 依赖，实际以 `APP=OFF` 构建；模块与依赖边界未改变。资产散列由采集层核查，驱动核查实际尺寸，两者共同落实输入冻结。

源码、程序、Cache、输入身份及原始结果归档于 `benchmark-output/cpu-cbt/cpu-cbt-01/`。完整字段与控制流见[代码事实](../../codebase/cpu_cbt/cpu_cbt_update.md)，验证、性能分析与规划比对见[阶段审查](../../reviews/cpu_cbt/cpu_cbt_01_serial_update_review.md)。本阶段闭环；CPU-CBT-02 尚未启动，质量评价能力仍暂停。
