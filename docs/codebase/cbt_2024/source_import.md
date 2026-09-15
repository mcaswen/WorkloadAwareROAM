# CBT 来源迁入事实

2026-09-16；范围为 CBI-01 已编译的 CPU 核心、公共声明及 HLSL。D3D12 运行文件已落盘，但尚未接通当前平台，不能将其视为已运行能力。

## 来源与差异

FACT：来源根为 `third_party/RoamTesting`，基线常量位于 `Cbt2024Baseline.h`。完整文件映射、来源原始字节 SHA256、CBI-01 目标 SHA256 和适配类别保存在 [source_manifest.json](source_manifest.json)。46 项包括 31 个算法文件、6 个 shader 文件、7 个来源测试、独立宏调色板与来源声明。来源目录未修改。

FACT：七个核心 `.cpp` 的算法内容保持来源；头文件的 `@brief` 改为自然摘要。程序化 shader 仅改变调色板 include；调色数值保持来源。新增声明及构建胶水不冒充来源代码。

FACT：`src/algorithms/cbt_2024/THIRD_PARTY_NOTICES.md` 保留来源的权利与发布限制。记录的上游为 `7351e6fb380acc149b3aef22a6c39bf3df7950a6`，兼容提交为 `7ae736d179528a0996449c0cc2db7f3279edc8ee`；本地迁移 SHA 才是本轮实际使用的文件证据。源目录中的 `official-baseline` 是来源身份名称，不表示当前适配未经修改。

## 文件、类型与数据边界

以下路径均相对 `src/algorithms/cbt_2024/`；完整扩展名及逐文件身份见清单。

| 文件组 | 类型及入口 | 当前职责与状态 |
|---|---|---|
| Cbt2024Baseline.h | OfficialBaselineV1、RuntimePathPreset | 冻结身份、来源路径预设、容量和 shader 入口；不控制当前 EIP |
| Cbt2024Support.h/.cpp | 设备能力判断 | 依赖图形能力接口，CBI-01 未编译；运行接通属于 CBI-02 |
| CbtOccupancyTree.h/.cpp | CbtOccupancyTree、CbtOccupancyCapacity | CPU 占用位、分层计数、按 rank 解码空闲/占用槽；调用者为测试和来源 CPU 参考 |
| CbtBisectorTopology.h/.cpp | CbtBaseTopology、CbtTopologyBufferLayout | 六个基础二分器、邻接、控制点、间接命令和缓冲尺寸 |
| CbtClassification.h/.cpp | CbtClassificationEvaluation、EvaluateCbtClassification | 投影面积、朝向、视锥和简化迟滞参考；真实高度首帧分类输入 |
| CbtSplitPlanner.h/.cpp | CbtSplitPlan、PlanCbtSplits | 依赖传播、模板 OR、容量预留及分配序号；不修改生产 GPU 状态 |
| CbtBisectCommit.h/.cpp | CbtBisectCommitResult、CommitCbtBisects | 输入代拷贝、四模板局部填写、统一外邻传播；返回拥有数组的 CPU 参考结果 |
| CbtSimplifyCommit.h/.cpp | CbtSimplifyCommitResult、CommitCbtSimplifications | 唯一两/四节点简化组、释放槽及外邻传播；返回独立结果 |
| CbtTerrainGeometry.h/.cpp | CbtTerrainGeometryResult、EvaluateCbtTerrainGeometry | 逻辑 LEB 路径、双线性高度、四点法线与父级分类第四点 |
| CbtGpuAbi.h、CbtGpuAbi.shared.h | 常量及共享布局 | C++/HLSL 共用偏移、状态/模板位；不依赖 renderer |
| CbtDebugVisualization.shared.h | 宏常量 | CBT shader 私有的来源调色板 |
| d3d12/D3D12CbtGpuState.h/.cpp | D3D12CbtGpuState | 来源 GPU 资源所有者；已迁入未编译 |
| d3d12/D3D12CbtOccupancyTree.h/.cpp | D3D12CbtOccupancyTree | 来源 GPU 树资源和操作；已迁入未编译 |
| d3d12/D3D12CbtFramePipeline.h/.cpp | D3D12CbtFramePipeline | 来源更新编排、状态转换；已迁入未编译 |
| d3d12/D3D12CbtGeometryPipeline.h/.cpp | D3D12CbtGeometryPipeline | 来源索引/几何生成；已迁入未编译 |
| d3d12/D3D12CbtDiagnostics.h/.cpp | D3D12CbtDiagnostics | 来源时间戳、延迟数量及诊断读回；已迁入未编译 |
| d3d12/D3D12CbtTerrainLodAlgorithm.h/.cpp | D3D12CbtTerrainLodAlgorithm | 来源算法适配；仍引用待映射的旧公共字段，不是当前可选算法 |

FACT：公共 `TerrainLodCbtTypes.h` 定义面积/容量/模式及统计区块，`TerrainLodSettings.Cbt` 和 `TerrainLodStats.Cbt` 以值持有。默认面积 50、容量 128K、验证 Off、几何 ModifiedOnly。GPU 统计带有提交代、样本代、资源代及故障恢复计数；当前 CPU 算法不会填充该区块。

FACT：`TerrainLodGpuOutput.h` 仅包含标准类型及借用描述，不含 D3D12 头或 COM 所有权。四类句柄、字节容量、跨度、indirect 偏移与寿命已经声明，但当前 RenderPacket 尚无 GPU 分支，工厂、GUI 和 renderer 仍只有原 CPU 路径。

FACT：当前 `HeightMap` 新增 `Values()` 只读 span，直接借用 `_heightValues`。数组来自与 `RawSamples()` 同一次成功加载；在下次成功加载或销毁后失效。加载失败仍保留旧源。未更改归一化、采样、世界尺寸或 revision 逻辑。

## 核心控制流与约束

FACT：占用树以 64 位 bitfield 为叶端；上层计数使用来源的分层紧凑表示。`Reduce` 从位图全量归约。rank 解码在计数树中选择分支，空闲解码使用容量减占用数。它是 CPU 校验实现，不是计划中的 CPU fallback。

FACT：`BuildSquareCbtBaseTopology` 创建 heap identity 8..13 的六个基础二分器。动态槽先排列，基础槽从动态容量偏移开始；容量不是活动三角形预算。初始 draw 为六面、18 顶点。代码中零基深度与 heap 位长分别有用途，不能合并或平移。

FACT：分类保持来源面积表达式及父级简化检查：超过面积阈值且未达最大深度时细分，低于半阈值或超过深度时考虑简化。朝向与视锥先过滤，投影面积包含来源的法线角度项。评分不是当前 ROAM 的屏幕误差 score。

FACT：split planning 先基于当前邻接传播模板需求，再为新槽计算保守预留。容量不足不提交该预留；多个请求可 OR 到共享模板，不重复增加首次分配节点。CPU allocator 从旧占用位图解码待用槽，提交后才体现新占用。

FACT：bisect 参考先复制输入数组，再填写保留槽和兄弟槽；所有模板完成后传播外部邻接。simplify 参考筛选唯一偶数身份的两/四节点组，发布保留节点、清除释放槽，最后传播。验证器检查活动身份、槽位范围和邻接双向关系。两者含全数组复制，所以不能以 CPU 参考成本描述 GPU 管线的工作量。

FACT：几何沿稳定 heap 路径解码参数域三角形，复用当前 `RoamGeometry` 的世界映射与双线性高度/法线。两份 `RoamGeometry` 的可执行内容相同；`TerrainMeshVertex` 为 52 字节，`CbtBisectorData` 为 32 字节，draw state 为 40 字节。

## 构建和调用者

FACT：`cmake/Cbt2024.cmake` 默认关闭。测试开启 CBT 后创建 `parallel_roam_cbt_core`，依赖当前 GLM、STB、HeightMap 和 TerrainLodView；八个测试入口链接同一库。GPU 源文件没有被该库或应用链接，不给未满足的依赖提供假实现。

FACT：D3D12 着色器目标使用当前固定 DXC。四容量分别编译 18 个 topology 与 7 个 occupancy 入口，另有 5 个公共 bootstrap 入口及 1 个程序化 VS，共 106 个输出。`CbtGpuAbi.shared.h` 是 C++/HLSL ABI 的唯一来源。

INFERENCE：CPU 树初始化/归约依赖容量；分类依赖候选数与 LEB 深度；参考提交的数组复制依赖槽池规模。当前阶段不存在 GPU 运行时间、多核收益或端到端性能结论。

## 验证范围与未完成项

FACT：八个定向测试覆盖占用解码、六面初始化、分类、split 预留、bisect、simplify、几何及 CPU 输入哈希隔离。四容量 DXC 构建通过。未运行项目完整测试集，因为 CBT 尚未改变原 CPU 算法逻辑。

PLANNED：设备能力、连续描述符、GPU output 验证、创建上下文、间接绘制、当前材质 ABI、GUI/CLI、资源切换及视觉验收在 CBI-02。GPU 实际同代网格捕获和 EIP 比较在 CBI-03/04。上述能力不能从本阶段编译或 CPU 测试推定。

性能快验和最终审查见 [CBI-01 规划结果](../../plans/cbt_2024/cbi_01_source_import_plan.md) 与 [审查](../../reviews/cbt_2024/cbi_01_source_import_review.md)。
