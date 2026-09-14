# 事务化 LOD 平台接入边界事实

> 2026-09-15；源码基线 `c7e6cd1`。范围是原型到公共算法接口、应用入口和两个上传消费者之间的接入边界，不重复整个几何内核。以下标为 FACT 的内容来自本次源码扫描；历史算法内部事实继承 [GWR](gwr_final_facts.md)、[GTP-04](gtp_04_multicore_facts.md) 和 [SVE-01](sve_01_scaling_facts.md)。目标方案另见[接入大规划](../../plans/cpu_refinement/transactional_platform_integration_plan.md)。

## 1. 当前入口与模块归属

| 文件/符号 | FACT：当前职责与限制 |
|---|---|
| `src/algorithms/ITerrainLodAlgorithm.h` | `TerrainLodAlgorithmId` 只有 Classic、DOD 和 Count；统一入口是 `BuildRenderData`、`Stats`、`Reset`，没有事务化算法注册 |
| `TerrainLodRenderPacket` | 支持 `BorrowedCpuMesh`、`UntilNextBuildOrReset`、独立顶点/索引更新范围、全量标志、generation；`HasConsistentResourceContract` 校验描述与范围，不证明借用对象实际存活 |
| `src/render/TerrainRenderer.cpp::CreateTerrainLodAlgorithm` | OpenGL 文件内工厂，只创建 Classic/DOD |
| `src/render/D3D12TerrainRenderer.cpp::CreateTerrainLodAlgorithm` | D3D12 文件内重复工厂，同样只创建 Classic/DOD |
| `src/benchmark/TerrainLodBenchmark.cpp::CreateAlgorithm/ExpandAlgorithmSelection` | 无窗口实验另有一份创建/枚举逻辑；部分实验语义仅适用于 Classic/DOD |
| `src/gui/ImGuiLayer.cpp` | `TerrainModeIndex`、`TerrainAlgorithmFromModeIndex`、`TerrainModeName` 和面板选项显式区分原有算法 |
| `src/app/Application.cpp` | `ToTerrainLodSettings`、`ApplyTerrainPanelSettings` 搬运配置；运行时实验顺序在 `BeginRuntimeBenchmark` 相关逻辑中固定为 Classic/DOD |
| `src/app/RuntimeBenchmark.cpp` | 通用总时间/上传报告与 Classic/DOD 五阶段分析共存，不能直接把事务化阶段填入原字段后声称同语义 |

公共接口依赖 terrain、视图与阶段值类型。当前不存在可供三个入口直接复用的统一算法注册表；工厂提取属于未来改动，不是当前事实。

## 2. 原型拥有的状态与执行流

FACT：`src/experiment/greedy_transactional_lod/TransactionalPipeline.h/.cpp` 持有 `TransactionalState`、`TransactionalSamples`、`TransactionalMesh` 和执行回调。正常调用链是：

```text
InitialMesh → Pipeline 构造 → Initialize
每轮 SetView → Update → Reservation::Plan → Apply
Apply: 拓扑准备 → 样本准备 → 网格准备 → 拓扑发布 → 派生状态发布
外部 ConsumeMesh → 获得借用数据与累计更新范围
```

`Initialize` 首次全 Q 与完整网格初建，随后幂等。`SetView` 允许矩阵/视口变化；拒绝场景、额度、预算、尺度、细分阈值和高度保护规则变化。相同矩阵/视口提前返回；换视图即使没有拓扑改变也会增加状态版本，并调用 `Mesh::AdvanceGeneration`。因此原型 generation 不是纯网格字节变化编号。

`Apply` 在空批次时返回。非空批次先核对版本，准备全部潜在失败工作，才共同发布；准备失败不应发布部分拓扑。`SetView` 与随后 `Update` 是两个发布边界，不能从单批原子性推断整个公共帧入口具有回滚能力。

FACT：核心并未调用 Legacy 获得后续目标。实验种子来自文件，`TransactionalInput::Load/Views` 依赖冻结场景、Formal 清单和 SVE 协议。文件输入、质量输出与全量验证只连到探针；正常平台没有资产到 `InitialMesh` 的内存初始化入口。

## 3. 原始高度、初始几何与视图限制

FACT：`src/terrain/HeightMap.cpp::LoadFromFile` 使用 `stbi_load_16`，将 uint16 除以 `65535.0F` 存入 float 数组，然后释放原始像素。公开接口只有 float `SamplePixel/SampleBilinear`、尺寸、路径；没有原始 uint16 视图或加载版本。

`TransactionalTypes.h::HeightSource` 则保存 uint16 原始样本；`TransactionalSamples` 要求至少 2×2 的方形参考，建立固定参数域 Q。双线性参考独立于当前拟合网格；不能用公共输出高度或旧 float 插值替代。

`tests/GreedyTransactionalLodFamilyProbe.cpp::Import` 已实现公共网格到中立几何的离线转换：以完全相等的 UV 共享点，要求同 UV 高度完全一致，使用输出 `Position.y`，按参数域方向规范面连接。不重采高度、不做容差焊接。当前它是探针局部函数，不是生产模块。

FACT：`BuildTerrainLodViewInput` 接收 `usesZeroToOneDepth` 并据此生成近裁剪平面，但没有把该布尔值存入返回结构。原型矩阵为行序；GLM 为列索引，现有实验逐元素拷贝。

FACT：原型 `Samples::Project` 使用 `-w ≤ z ≤ w`，被测近面检查为 `z ≥ -w`；`Certification::ExactError`、`ErrorBounds` 及 `Fit` 的近面约束同样使用 `z+w`。因此只换成 D3D12 的矩阵还不够，所有这些判断必须使用一致深度约定。参考可见而被测样本越过近面时，目前会抛错，不存在通用任意视点裁剪修复算法。

## 4. 调度与构建依赖

FACT：`TransactionalExecution::Run` 接收同步分块回调，自己不拥有线程池；按固定连续区间填写私有结果，barrier 后归并账本。`Diagnostics=false` 不记录真实线程身份，但仍保留逻辑计数和正常阶段时间。

探针拥有 `experiment/roam_materialization/MaterializationExecutor`，后者包装 `algorithms/data_oriented_roam/DataOrientedRoamThreadPool`。普通任务异常在独占槽保存，排空后重抛；入队异常会 `Shutdown` 并将执行器标记不可再用。基础线程池没有拓扑状态，但物理目录/命名属于 DOD；直接向公共工具层引用它会产生反向依赖。

FACT：`tests/CMakeLists.txt` 内才定义 `PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD` 和 `parallel_roam_transactional_lod_core`。核心清单目前包含 `TransactionalDynamicReference.cpp`。Boost 固定 `1.90.0`，显式包含路径，MSVC 使用 `/fp:strict`，其他编译器使用 `-fno-fast-math -ffp-contract=off`。正式应用目标没有链接该库；仅给应用加一个枚举值不能完成接入。

## 5. 渲染调度和更新范围生命周期

FACT：两个 renderer 的 `UpdateForView` 都可能在非脏、视图变化不足时跳过算法入口。`usesRoamView` 仅列 Classic/DOD。原型每轮是有限批次，现有跳过机制无法自动表达“静止相机仍继续下一批”。

`RebuildTerrainLod` 创建/更换算法后调用公共入口，校验渲染包，保存借用 mesh，随后调用对应后端的 `UploadMeshData`。资产重载会重置算法。上传失败设置 `_meshDirty`，但公共算法接口没有上传确认回调；不能假定 `ConsumeMesh` 清理后的范围会自动重新出现。

FACT：`TransactionalMesh::Consume` 先生成范围，再清空 Pending；第一次消费为全量。顶点按物理面槽持久保存，活动索引是紧凑序列，两组范围不能按相同下标强行配对。`Build` 从实际拟合点构造 float 位置、面法线和高度；当前只显式检查位置有限，binary64 几何合法不能自动推出 float 光栅输入非退化。

OpenGL：`UploadMeshData` 按容量决定分配/全量上传，否则对顶点和索引分别提交脏范围，随后更新绘制数量。D3D12：该方法增加 renderer 自己的网格版本，将范围累积至每个帧资源；轮转时补齐当前帧资源，容量与 fence 属于该后端。它们不是同一个上传实现。

INFERENCE：接入可复用已有借用与范围契约，正常包装有望只做范围规模工作；但范围移交后的失败恢复、视图版本与输出版本区分、资源轮转必须新增定向验证，尚无“零额外复制/零等待”的实测保证。

## 6. 实验与证据边界

运行时实验复用 Application 的离散相机和 renderer；无窗口实验直接调用公共算法。二者与旧 SVE 种子/八轮协议不是自动相同的输入。平台现有五阶段配对、严格 Legacy 对照和上传配对有各自适用范围，不能仅因新算法可创建就全部纳入。

SVE 事实：增长 200k/C8 移动均值 131.656ms，预留 78.912ms；固定 64 的三个大档零事务；增长组跨视图采样误差约 64～67px。它们限制性能/质量主张，不表示平台适配已经失败或证明一般算法无效。没有平台实际帧时间，不能把历史 WSL 数字当作原生 Windows GUI 基线。

## 7. 未确认事项

- **UNCERTAIN**：一次当前相机的公共 DOD 初始化是否在所有拟接入设置下产生可接受种子；目前只有冻结来源证据，没有从应用冷启动的系统验证。
- **UNCERTAIN**：真实连续相机何时触发近面异常，及 float 输出几何误差相对原型状态的影响。
- **UNCERTAIN**：两个后端消费新范围、跳过若干帧、上传失败和资源重建的行为，尚未运行接入测试。
- **UNCERTAIN**：当前机器的 WSL 图形、Windows 原生工具链和 D3D12 运行能力；本次只读规划扫描不等于环境验收。
- 本页未扫描所有 renderer 资源初始化细节；后端资源/fence 的原有实现由原模块负责，实际修改前按对应小规划补读被修改路径。
