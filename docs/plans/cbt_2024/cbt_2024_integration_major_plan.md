# CBT-2024 参考实现接入大规划

日期：2026-09-16。编号：CBI。状态：用户已确认，按小阶段自主闭环并逐阶段提交。

用户补充：新增/适配代码参照 DOD 风格，不沿用 Transactional 紧凑排版；小规划、验证与提交在本任务内自主完成，不逐阶段重复请求确认。

## 1. 目标与授权边界

将 `third_party/RoamTesting/src/algorithms/cbt_2024` 中已经接入同源框架的完整实现迁入当前平台，保留其 GPU 更新语义，作为 Classic / DOD / Transactional 之外的独立比较算法。最终通过已有实验基础设施，取得 CBT 实际输出的屏幕空间误差、实际三角形数及执行成本。

本轮是现成实现的迁移与比较接入，不是重新实现 CBT，也不恢复已归档的 CPU-CBT。当前研究主线仍为 CPU LOD；CBT 的 GPU 管线是参考对象，不在本轮设计新的 GPU 算法。

完成目标：

1. D3D12 中可独立选择 CBT，正常更新、暂停、单步、Reset、切换和渲染。
2. 核心代码、着色器、ABI 和更新顺序具有可追溯的来源与迁移差异记录。
3. 普通路径保持 GPU 常驻更新和间接绘制；质量采集能够取得指定更新代的实际完整活动网格。
4. 沿用原始高度样本＋双线性参考，在相同地形、相机与分辨率下形成第一份小规模质量对照。
5. 原三种 CPU 算法的输出、设置、借用网格生命周期及性能不因接入发生未解释退化。

不包含：OpenGL CBT 移植、GPU 优化、CPU-CBT 复活、统一两后端上传实现、重写 profiler、新地形或大规模实验矩阵，以及将 CBT 改造成 ROAM 硬预算或误差驱动算法。接入成功不代表新算法具有质量优势。

按 CBI-01 → CBI-04 顺序展开小规划。用户已授权实施和阶段提交；不包含远程推送及公开发布。

## 2. 依据与当前代码事实

已核对[规划规范](../plan_guideline.md)、[开发规范](../../standards/development_guidelines.md)、[审查规范](../../reviews/review_guideline.md)，以及以下来源：

- [来源接入规划与实现记录](../../../third_party/RoamTesting/docs/parallel-roam/16-cbt-2024-integration-plan.md)：完整 CBT GPU 流程及各阶段历史验证。
- [来源身份常量](../../../third_party/RoamTesting/src/algorithms/cbt_2024/Cbt2024Baseline.h)、[来源适配器](../../../third_party/RoamTesting/src/algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.cpp)、[来源诊断](../../../third_party/RoamTesting/src/algorithms/cbt_2024/d3d12/D3D12CbtDiagnostics.h)。
- 当前[算法接口](../../../src/algorithms/ITerrainLodAlgorithm.h)、[注册表](../../../src/algorithms/TerrainLodAlgorithmRegistry.h)、[D3D12 后端](../../../src/render/D3D12GraphicsBackend.h)、[平台回放](../../../src/benchmark/experiment/ExperimentPlatformRun.cpp)。
- [EIP 回放事实](../../codebase/experiment_infrastructure/eip_05_replay_facts.md)、[质量与计量事实](../../codebase/experiment_infrastructure/eip_06_analysis_facts.md)、[使用手册](../../codebase/experiment_infrastructure/usage.md)、[FER-01 报告](../../research/experiment_infrastructure/fer_01_results.md)。

| 已有能力 | 当前接入缺口与处理 |
|---|---|
| 来源包含占用树、分类、规划、分配、四类细分模板、简化、传播、索引、几何及 GPU 诊断 | 整组迁入，适配公共类型与构建依赖；不重新推导核心算法 |
| 当前接口有三种 CPU 算法、`CpuMesh` / `DebugOnly` 及借用 CPU 网格 | 增加 GPU 间接输出分支，保留 CPU 原有契约 |
| 当前 D3D12 已有设备、命令列表、单槽描述符、立即提交及帧捕获 | 补来源需要的连续描述符分配与设备能力查询；不用替换整个后端 |
| 来源 GPU 绘制从活动索引读取物理槽位，再读取每槽三个顶点 | 迁入间接绘制消费者；CPU 延迟计数不控制当前 draw count |
| 两份工程已有固定 DXC / Agility runtime；所核对的二进制哈希相同 | 复用当前固定依赖，增加 CBT 着色器构建与部署，不另建工具链 |
| EIP 当前每帧要求 CPU 网格，并按 `Case.Budget` 检查数量 | 增加 GPU 证据分支和算法专属容量校验；普通 CBT 计时不得强迫全量读回 |
| 来源诊断仅回读计数、时间及有限基础槽验证 | 不能用作完整质量网格；另补仅实验启用的实际 GPU 网格捕获 |

来源基线为 `cbt-2024-official-baseline-v1`，记录的上游提交为 `7351e6fb380acc149b3aef22a6c39bf3df7950a6`，兼容补丁提交为 `7ae736d179528a0996449c0cc2db7f3279edc8ee`。这些是来源声明；CBI-01 另外冻结本地实际文件 SHA、来源仓库身份和迁移差异，不将本地适配器直接称为未经修改的官方程序。

来源 [THIRD_PARTY_NOTICES](../../../third_party/RoamTesting/THIRD_PARTY_NOTICES.md) 已明确 CBT 派生部分的公开再分发限制。本地研究迁移保留该记录；根 MIT 不覆盖上游未明确授权的材料，本规划不授权公开发布或打包这部分源码。

## 3. 必须保持的算法与比较契约

### 3.1 迁移保真

沿用来源的投影面积分类、简化阈值关系、父级防振荡检查、容量预留、模板提交、邻接传播、占用归约及几何生成。保留完整帧顺序：

```text
Reset → Classify → Split planning → Allocate
→ 邻接输入复制 → Bisect → PropagateBisect
→ PrepareSimplify → Simplify → PropagateSimplify
→ Reduce → 邻接代切换 → Indexation / indirect preparation
→ 来源定义的几何更新 → GPU indirect render
```

精确 dispatch、barrier、资源状态和索引/几何顺序以来源 `D3D12CbtFramePipeline` 为准，上图只是职责摘要。不能用 CPU 原型的轮次编排替换它。保留 `ModifiedOnly` 正常模式、`FullDebug` 诊断模式，默认绘制来源的完整活动列表，不能悄悄改成仅可见列表。

允许的迁移修改是公共类型拆分、工厂注入、路径/构建适配、当前材质常量对接和实验观测。逐项记录理由；涉及分类、阈值、细分/简化、浮点策略或并发顺序的变更必须独立 Review，不能隐藏在“接入修复”中。

### 3.2 设置独立

- `capacity` 沿用 128K / 256K / 512K / 1M；它是来源定义的动态槽池容量，不等于当前活动三角形数，更不等于 ROAM 的硬预算。
- `triangleAreaPixels` 沿用来源单位与公式，不映射为 ROAM 的 split / merge 屏幕误差阈值。
- CBT 的 `maxDepth` 按来源 HeapID 位长与基础深度解释；跨算法填写同一数字不代表同一最细网格。
- 普通参数默认值与来源预设分别登记；实验显式展开实际值，不能把来源预设中的 58 / 2.05 等面积值误当为全工程默认值。
- CBT 不使用 DOD 种子，不取得 Legacy 的目标 J；从来源初始状态按冻结更新机会独立演化。

### 3.3 质量评价

Primary reference 仍为原始高度样本＋冻结双线性插值，不使用 CBT 自身网格、DOD 或规则固定对角线网格替代 ground truth。

沿用 `sampledScreenMaxPx`、当前 evaluator 的 RMS 权重语义、`Hmax` 和逐点 `Dmax`。其中 RMS 当前是可见参数域样本等权，不能改称屏幕面积加权；`Dmax(CBT, DOD)` 必须比较同一组有效样本上的误差差值，不能用两个全局最大值相减。

主对照是误差—实际三角形数，而非“同 capacity / 同 threshold”。报告同时记录 CPU 硬预算及利用率、CBT 池占用与是否饱和、活动 N、更新机会和视图。实际 N 不接近时只能比较曲线上的设计点，不能宣称同预算或同工作量优势。

### 3.4 时间与更新机会

CPU 提交、GPU 计算、GPU 绘制、等待、整帧墙钟、初始化及证据读回分别记录。CBT 的 `BuildRenderData` 墙钟只是 CPU 侧组织/记录命令成本，不能作为与 DOD 完整 CPU 更新的同义指标。

正常性能沿用异步执行，不为获得即时统计而每帧等待。GPU 阶段时间与数量按来源 sample generation 对齐；缺失、延迟或跨代数据留空/标注，不归到当前帧。重叠的 CPU/GPU 区间不直接相加成“总时间”。

相机按 EIP 机会推进，各算法每个机会执行一次自身合法更新，静止和返回阶段仍持续运行。不能为 CBT 多执行隐藏追赶轮，也不能因相机不动跳过来源要求的更新。稳态测试若需要额外轮次，作为独立协议记录，不混入连续轨迹。

## 4. 目标架构与依赖方向

```text
GUI / CLI / EIP case
       ↓ 设置与创建上下文
算法注册表 / 工厂
       ├─ Classic / DOD / Transactional → 原 CPU RenderPacket
       └─ CBT adapter → GPU state / frame pipeline / geometry / diagnostics
                                ↓ GPU RenderPacket（借用资源）
                         D3D12 间接绘制消费者

EIP 质量模式 → GPU mesh capture → 自有 CPU mesh artifact
                                     ↓
                         现有独立质量 evaluator / 图表 / 报告
```

算法拥有拓扑、几何 GPU 缓冲和运行状态；后端拥有设备、命令队列、共享描述符堆与帧同步；渲染消费者只拥有绘制所需 PSO、绑定及查询资源。EIP 捕获器只拥有读回暂存，不接管算法状态，不参与候选或更新决策。

### 4.1 抽象归属

| 选择 | 对象 | 职责与理由 |
|---|---|---|
| Reuse | 来源 `cbt_2024/` 核心及 `d3d12/` 六组组件 | 保留已有算法与资源所有者，不再造 CPU/GPU 参考实现 |
| Reuse | 当前 `PinnedD3D12Runtime`、帧循环、材质与截图 | 来源和当前已有同类能力，避免第二套 runtime 或捕获系统 |
| Extend | 公共算法接口、注册表、设置与可用性 | 增加 GPU 输出和创建上下文，CPU 调用方保持原路径 |
| Create | `TerrainLodCbtTypes.h` | 放声明层 CBT 参数和统计，避免将几十个字段散入公共大结构；不依赖具体实现 |
| Create | `TerrainLodGpuOutput.h` | 独立定义 API、借用句柄、容量/跨度、indirect offset、资源代与拓扑代；公共头不引入 D3D12 头 |
| Extend | `GraphicsBackend` / `D3D12GraphicsBackend` | 来源式设备能力与连续描述符；不加入算法控制逻辑 |
| Wrap | `D3D12CbtRenderPass` | 将来源消费者的绘制绑定/间接绘制部分封装到独立文件，避免继续堆入大型 renderer |
| Create | 实验专用 `D3D12CbtMeshCapture` | 读取实际 GPU 输出并转为现有 mesh artifact；只读观测，与普通帧诊断分开 |
| Extend | EIP case、回放、结果适配与图表 | 添加 CBT 参数/异步计量/证据类型，复用原输入身份、相机与质量评价 |

采用既有 Adapter 与 Strategy 关系、RAII 资源所有者及显式输出类型分派。无需插件系统、通用渲染图、通用 GPU 算法层或第二套实验平台。

### 4.2 关键接口

- 工厂保留 CPU 无设备创建入口，增加可选 `TerrainLodCreationContext`，只借用 `IGraphicsBackend`。具体 `.cpp` 按构建开关创建 D3D12 CBT；公共头只前向声明后端。
- 区分“名称已知”“本构建支持”“当前设备支持”。OpenGL / CPU-only 明确拒绝 CBT；设备不满足来源 SM6.6、64 位运算/原子能力时报告原因，不静默替换算法。
- `TerrainLodRenderPacket` 增加 `GpuProceduralIndirect` 分支及 GPU 描述。验证句柄、容量、跨度、indirect 布局和生命周期，不要求当前 CPU 已知道非零 draw count。
- `RequiresContinuousUpdate` 复用当前能力字段，不为迁入历史 `EveryFrame` 枚举再造一套调度。
- 公共统计增加独立 CBT 区块，来源统计完整映射；GUI / CSV 可以选择展示子集，不能丢失 generation、故障与容量饱和信息。
- renderer 提供只读 GPU 输出诊断视图；EIP 不向下获取 CBT 私有控制器。CPU 的 `CurrentCpuMeshForDiagnostics` 保留原语义。

### 4.3 生命周期

1. 设备初始化并通过能力门禁后创建 CBT；资源初始化/容量变化的等待只在明确冷路径计费。
2. 正常帧仍为 `BeginFrame → UpdateForView → Render → Present`，GPU 更新录制在开放的帧命令列表内。
3. `ResourceGeneration` 只在资源替换时变化，`TopologyGeneration` 对应更新；描述符绑定不能随每次拓扑更新全量重建。
4. 暂停不推进算法代次，单步只推进一次；相机/材质切换与 Reset 的既有控制规则保持。
5. 算法切换、Reset、容量/地形更换与退出时，先保证旧资源的 GPU 使用已结束，再释放借用绑定、算法资源和后端。保留来源声明的内部销毁顺序。
6. 来源故障锁存/恢复仍可保留，但错误与恢复次数必须进入证据；发生自动重建的运行不能被当作不间断、无故障样本。

## 5. 文件规划

以下为目标文件归属；小规划只展开该阶段实际修改，路径粒度调整写回本表，职责与接口变化需重新 Review。

```text
src/algorithms/
  TerrainLodCbtTypes.h                     新增：CBT 设置/统计声明
  TerrainLodGpuOutput.h                    新增：GPU 输出声明
  ITerrainLodAlgorithm.h                   扩展：类型化输出与参数/统计区块
  TerrainLodAlgorithmRegistry.h/.cpp       扩展：身份、可用性、设备上下文
  cbt_2024/                               迁入：来源相同目录与核心文件
    d3d12/                                迁入：来源资源/帧/几何/诊断/适配器
src/render/
  GraphicsBackend.h                       扩展：设备能力声明
  D3D12GraphicsBackend.h/.cpp              扩展：查询与连续描述符分配
  D3D12CbtRenderPass.h/.cpp                新增：来源 GPU 绘制消费者封装
  TerrainRenderer.h                       扩展：设置与诊断输出入口
  D3D12TerrainRenderer.cpp                 扩展：输出分派与生命周期挂接
  TerrainRenderer.cpp                     窄修改：不支持分支和公共设置兼容
src/benchmark/experiment/d3d12/
  D3D12CbtMeshCapture.h/.cpp               新增：质量模式 GPU 输出捕获
cmake/Cbt2024.cmake                       新增：模块、着色器、测试构建清单
assets/shaders/dx12/
  CbtProceduralTerrain.hlsl                迁入：实际间接顶点消费
  cbt/*.hlsl / *.hlsli                     迁入：来源计算着色器与 ABI 包装
tests/Cbt*Tests.cpp                        迁入：七组已有核心测试
tests/CbtIntegrationContractTests.cpp      新增：公共接口/证据代次边界测试
configs/experiments/cases/cbi/             新增：小规模冻结比较配置
docs/plans/cbt_2024/                       本规划与逐阶段小规划
docs/codebase/cbt_2024/                    来源差异、接口事实与使用说明
docs/reviews/cbt_2024/                     各阶段审查/性能问题记录
docs/research/cbt_2024/                    首轮比较与聚合证据
```

核心迁入清单按原职责保留：`Cbt2024Baseline/Support`、`CbtGpuAbi(.shared)`、`CbtOccupancyTree`、`CbtClassification`、`CbtBisectorTopology`、`CbtSplitPlanner`、`CbtBisectCommit`、`CbtSimplifyCommit`、`CbtTerrainGeometry`；D3D12 目录保留 `TerrainLodAlgorithm`、`GpuState`、`FramePipeline`、`GeometryPipeline`、`Diagnostics`、`OccupancyTree` 六组。

其他现有文件的有限接点：

- `CMakeLists.txt`、`cmake/ProjectOptions.cmake`、`CMakePresets.json`：模块开关与独立 D3D12 构建，不复制来源整份 CMake。
- `src/app/ApplicationCommandLine.*`、`Application.*`、`src/gui/ImGuiLayer.*`：算法选择、CBT 参数与可用性；不复制历史 UI/controller。
- `src/experiment/infrastructure/ExperimentCase.*`、`configs/experiments/schema/case.schema.json`：显式 CBT 设置与适用性校验。
- `src/benchmark/experiment/ExperimentReplay.*`、`ExperimentPlatformRun.cpp`：GPU 捕获/计量分支；CPU 入口拒绝 CBT。
- `scripts/experiment_infrastructure/` 内现有解析、运行、质量、归约及报告模块：新协议版本、空值语义和小规模比较图，不建立平行脚本体系。
- 来源许可证/来源说明随迁移记录保留；临时 `.cso`、工具链、读回原始网格与截图进入既有构建/忽略输出目录，不整仓复制 `RoamTesting`。

CBI-01 冻结全部 include 与 shader 依赖闭包。当前读到的 OCBT/ABI 包装已在来源模块内，不能仅因来源研究过 `large_cbt` 就把该仓库整体设为运行依赖。只迁实际必要的共享声明；已有 `RoamGeometry`、`HeightMap`、`TerrainMeshVertex`、调色板声明先核对再复用。

来源业务注释按当前规范人工逐文件检查，接口摘要用 `/// <summary>`；必要修订与算法差异分开登记，不做脚本式批量注释改写。

## 6. 实际 GPU 网格采集与实验适配

### 6.1 捕获契约

质量/视觉模式在指定机会完成更新与绘制后、下一次更新之前请求捕获，绑定 `(run, frame, pose, projection, resourceGeneration, topologyGeneration)`。首版允许显式等待完成再执行只读复制，优先保证证据可靠；这部分全部记入采集成本，不进入正常性能结论。

捕获器从同一代取得 draw state、活动槽索引与实际 `TerrainMeshVertex` 缓冲，按绘制同样的 `physicalSlot * 3 + localVertex` 规则重组自有 CPU 网格。核对 draw 顶点数、活动数、槽位范围、重复活动槽、ABI 跨度、有限值及覆盖；不能把未活动槽、仅可见列表或上一代计数混入。

首版可以复制池容量对应的缓冲，再由已捕获活动数裁剪，明确记录 O(capacity) 读回字节、时间和峰值内存；不为实验捕获新增生产 GPU 压缩算法。实际输出顶点是质量事实，CPU `EvaluateCbtTerrainGeometry` 只作有限定位对照，不能代替 GPU 输出。

读回资源状态按来源发布状态进入/恢复，捕获期间禁止下一代 mutation。不重跑一次算法来获得网格，不因捕获而 Reset，也不通过空更新“追到”统计代次。

### 6.2 普通计时与证据协议

CBT 普通计时没有完整 CPU 网格 hash；该列为空并写明证据类型。保留来源小型异步计数/时间读回，记录开关、字节和采样代次。质量帧才具有当前实际 N / mesh hash。EIP 不能为了保持旧 CSV 形状而填入假零、旧网格或同步读回。

对 EIP 采用版本化扩展：旧 CPU 产物仍可读取，新 GPU 产物显式包含执行设备、容量、面积、深度、采样年龄、当前输出代/统计代、故障/饱和以及 CPU/GPU 不同时间字段。原 `mesh.N <= Case.Budget` 仅用于有硬预算契约的算法；CBT 按来源槽容量与基础面规则检查资源合法性。

GPU 原子分配可能产生不同物理顺序，不能直接套用 CPU 的逐字节 hash 一致门禁。核心确定性夹具仍精确比对；运行比较以逻辑输出、结构不变量和实际误差为依据。若独立重复出现逻辑差异，保留全部结果和来源一致性边界，不挑一个最好网格，也不擅自强制串行化 GPU 来制造确定性。

## 7. 分阶段实施

| 阶段 | 实施内容 | 独立验收与性能对照 | 当前状态 |
|---|---|---|---|
| **CBI-01 来源冻结与模块迁入** | 记录源文件/ABI/依赖/版权；迁入核心、着色器和测试；公共声明、编译门禁与构建接通；尚不默认启用 GUI CBT | 七组来源核心测试按职责覆盖；四容量着色器可编译；OpenGL/CPU-only 不误链 D3D12。修改前后同机短 CPU 基线；核心有限测试旧/新成本和构建成本分别记录 | 未开始 |
| **CBI-02 D3D12 持续接入** | 设备能力、描述符、工厂、GPU 输出、间接绘制、GUI/CLI及生命周期；保留当前材质 | test129 短序列与 Peking 既有返回轨迹；实际看填充/线框、暂停/单步/Reset/切换；来源与迁入版本同条件迁移对照；CPU D3D12/GL 受影响路径前后快验 | 未开始 |
| **CBI-03 实验网格与计量接入** | 同代实际 GPU 网格捕获；case/CSV协议、容量校验、独立质量与视觉证据 | 小网格 CPU 几何 oracle 与实际 GPU 位置核对；Peking 关键帧的截图/矩阵/活动 N/mesh 对齐；关闭捕获前后性能；开启捕获的读回成本单列 | 未开始 |
| **CBI-04 首轮有限质量比较与收口** | 冻结小面板与 CBT 参数点，复用 EIP 生成质量—N图、轨迹/见证图与分边界时间表；沉淀命令/限制 | 同后端同输入重新取得必要 CPU 对照；真实画面验收；不以 CBT 或 Transactional 胜负为接入门禁。报告生成改动核对前后成本 | 未开始 |

每阶段先落小规划、保存实现前基线，再实现、定向验证、回写结果及架构审查，完成后单独提交；提交授权按用户对本大规划后续实施的确认执行，不把编写规划当作当前提交授权。提交信息说明变化，不加入“测试通过”等消息。完成当前阶段闭环后再开始下一阶段，不先铺完全部实现再补文档。

### 7.1 开发验证规模

CBI-01/02 使用 test129 / Peking 现有自然输入，先做能覆盖风险的短序列；不重跑所有历史 CTest、CPU-CBT、FER 或性能压力矩阵。CBI-02 的小容量/大容量实例覆盖资源切换，其余容量以构建、ABI和占用树用例覆盖，出现具体问题才补 GPU 用例。

每小规划写明预计耗时、具体配置、产物路径和停止条件。性能进程顺序运行，测量时不并行构建。依照开发规范 §7.3，先一组独立进程前后快验，工程筛查阈值为 `max(0.05 ms, 5% × baseline, 已有波动范围)`；有持续变化或具体风险才定向复测一次。没有波动估计即注明，不扣微秒，也不声称小样本显著性。

迁移前来源程序如不能直接运行，先记录构建/环境阻碍，仍可用源码/核心夹具开展接入，但不得把来源的历史硬件数字追认为本机性能基线。来源与当前的 GPU runtime、驱动、硬件、分辨率、输入、设置、诊断模式全部冻结。

### 7.2 首轮质量面板

CBI-04 只取已有 **Peking547 与 dem-canyon513** 两个资产，复用 FER 对应相机路线、尺度和参考采样。Peking 的 `TerrainSize=80 / HeightScale=12` 保持；其他设置从已冻结资产配置读取，不手写统一尺度。test129 保留为接入正确性输入，不另加到比较矩阵。

所有算法使用 D3D12 同一投影约定，CPU 参考为当前 DOD8 / Transactional8，Classic 接口仍完成兼容验证，但本轮不增加完整 Classic 研究矩阵。既有 OpenGL FER 数据可作背景，不直接并入本轮同条件比较。

每资产先至多三个 CBT 面积点，池容量固定为不明显截断目标区间的一个来源档位，并记录饱和。允许在**只查看实际 N、尚未求质量**的有限校准中确定面积点，固定校准轮数/停止条件并保留所有尝试；不得假设面积—N 严格单调。目标覆盖已有 50k～200k 的可比较部分，覆盖不到如实报告，不无限调参追齐 N。CPU 只选已有预算点中与该范围重叠的至多两个点，保证面板有限。

参数、更新机会数、冷启动/预热、关键帧、采样密度在求质量前写入 CBI-04。沿用来源初始化，报告初期收敛；不把容量饱和、未收敛或深度封顶造成的数量接近误当为算法公平性已满足。

先一组完整回放作为描述性比较；需要确认关键结果时只定向增加独立运行，并明确不做正式论文统计推断。GPU重复的质量绑定各自实际捕获网格，不能默认沿用另一运行的网格。输出包含：

- `sampledScreenMaxPx / Dmax / Hmax` 对实际 N 的散点与关键帧表；不从少数点虚构连续前沿或阈值证明。
- 固定相机轨迹的 N、可见误差与返回恢复曲线，标记冷启动、饱和和失败。
- 同视图真实画面、线框及最大误差见证；显示用稀疏热图不代替完整 Q 指标。
- CPU 组织、GPU 更新/绘制、等待和整帧时间分别展示；不把跨 CPU/GPU 的差异叫作同任务多核加速。

## 8. 视觉、正确性与退出条件

视觉验收必须使用实际 D3D12 截图并逐张查看：地形方向/尺度、高度、UV/中性材质、裁剪与线框、相机返回、非空更新和算法切换。不能仅以程序退出码替代视觉结论，也不要求与 OpenGL 逐像素相同。

以下问题阻断相应阶段：能力不足、来源必需依赖缺失、ABI/资源生命周期不明、持续 GPU 故障、捕获代次不一致、真实活动 mesh 无法恢复、源样本或矩阵不一致。先给有限复现与事实报告，不继续扩容、增轮或改变 CBT 分类来掩盖失败。

工程性能退化按[规划规范 §12.2](../plan_guideline.md#122-性能问题分析与修复规划须经用户确认)独立记录原因和修复归属；没有已获授权的修复范围时提交用户决定，不能静默带病进入后续阶段。CBT 自身比某 CPU 算法快或慢则属于研究结果，不自动构成接入缺陷。

主要风险与备选：

| 风险 | 本轮处理 |
|---|---|
| 两份公共接口差异大 | 迁核心、定向适配，禁止覆盖整份 renderer / interface / Application |
| 材质常量或 52 字节顶点 ABI 漂移 | 显式布局断言，计算 shader 与程序化 VS分层核对；材质只适配展示，不改位置生成 |
| GPU 当前输出与延迟统计错配 | 当前捕获与历史诊断分开，代次匹配失败不产出质量比较 |
| 原子顺序导致逻辑差异 | 保留来源行为和重复证据，不承诺 bitwise deterministic |
| 池容量/最大深度主导质量 | 报告饱和及深度条件，不称同硬预算对照 |
| 间接绘制接入后普通帧偷偷全量读回 | 计时路径无完整 CPU mesh；质量捕获显式启用并单独计费 |
| 可用性较好但新算法无质量优势 | 诚实关闭本轮比较，不追加 GPU 优化或改阈值追求正结果 |

## 9. 交付与状态

源码/着色器/小规模配置、迁移清单、适配差异、审查及聚合报告进入版本管理；原始运行写入 `benchmark-output/cbt-2024/cbi-XX/`，使用独立目录，不覆盖历史数据。报告保存生成命令、构建/程序/素材/相机身份与 GPU 环境。

最终分别标记：模块迁移、D3D12 平台接入、同代质量采集、首轮比较完成。质量优势、端到端竞争力与论文结论另列，不使用无类型的“PASS”。

本轮最终架构为：**来源 CBT 核心 → 当前 GPU 输出契约 → D3D12 既有帧链；实验只读捕获 → 现有独立质量评价。** 主要新增工作集中在接口适配与实际输出取证；不再建设另一套 LOD 算法或实验平台。

### 实现情况

- CBI-01：未开始。
- CBI-02：未开始。
- CBI-03：未开始。
- CBI-04：未开始。
- CBI-01 开始前已完成源码边界核对；阶段结果分别回填，后续未完成项不预先标记通过。
