# Milestones

> 当前说明（2026-08-18）：本文件记录工程初始化和早期实验演进。带日期的“实现状态”和 4A-4I 阶段记录是历史快照；当前正式基线只包含 Classic 与 Data-Oriented CPU ROAM。

当前基线保留阶段 0-5 的历史记录，并以 Classic/DOD 固定相机 runtime benchmark、双后端 CPU mesh 渲染和自动拓扑验证作为后续研究起点。

核心原则：每一步都应有一个可运行、可截图、可录制、可验证的阶段成果。

## 阶段 0：工程初始化与最小渲染闭环

### 目标

搭建 SDL2 + CMake + OpenGL 基础工程，确保开发环境稳定。

### 任务

- 配置 CMake；
- SDL2 创建窗口与 OpenGL Context；
- 接入 GLAD/GLEW；
- 接入 GLM；
- 接入 Dear ImGui；
- 实现 `Application` 主循环；
- 实现 FPS 相机、鼠标视角、WASD 移动；
- Shader 编译、日志输出和错误处理；
- 创建基础地面三角形，验证渲染链路。

### 验收标准

- 程序可编译、可启动；
- 可自由移动相机；
- 可显示三角形或简单平面；
- ImGui 面板可显示 FPS 与窗口尺寸。

### 当前实现状态（2026-07-03）

- 已完成 SDL2 窗口、OpenGL Context、GLAD 加载、GLM 矩阵、Dear ImGui 调试面板和最小渲染闭环；
- 已建立 `Application`、`platform::Window`、`render::Shader`、`render::TriangleRenderer`、`gui::ImGuiLayer`、`CameraController` 等基础模块；
- `debug-fetch` preset 会拉取/接入完整依赖并构建可交互应用；
- 默认 `debug` preset 在依赖不完整时保持 bootstrap 模式，用于验证基础配置，不强制联网拉取全部库；
- 当前渲染内容是一个地面三角形占位，用于验证相机、shader、深度测试和 UI overlay，阶段 1 再替换为 height map terrain mesh。

### 运行方式

```bash
cmake --preset debug-fetch
cmake --build --preset debug-fetch
./build/debug-fetch/bin/ParallelROAM
```

自动启动验证可使用 `./build/debug-fetch/bin/ParallelROAM --smoke-test`，程序会渲染数帧后退出。

交互方式：`W/A/S/D` 移动，`Space/Ctrl` 升降，按住鼠标右键移动视角，`Shift` 加速，`Esc` 退出。

## 阶段 1：地形显示 + 基础 UI

### 目标

先实现“没有 ROAM 的标准 Height Map 地形”，建立可视化与渲染基线。

### 任务

- 加载灰度 Height Map；
- 创建规则网格 terrain mesh；
- 根据 Height Map 采样高度；
- 计算基础法线；
- 实现 Phong 或 Blinn-Phong 光照；
- 加载简单地表纹理；
- 支持 wireframe；
- 支持 terrain size、height scale 调整；
- UI 显示 FPS、Draw Call、顶点数 / 三角形数、Height Scale、光照参数和 wireframe 开关。

### 验收标准

- 可从多个角度观察起伏地形；
- 地形无明显法线错误；
- 线框与实体模式均正确；
- Height Map 与世界坐标映射明确；
- 可截图作为报告“基础渲染模块”证据。

### 当前实现状态（2026-07-05）

- 已完成基于 Height Map 的规则网格 terrain baseline；
- `HeightMap` 使用 stb 读取灰度图片，并统一归一化到 `0..1`；
- `TerrainMeshBuilder` 根据 Height Map 生成规则网格顶点、索引、UV 和基础法线；
- `TerrainRenderer` 负责 OpenGL VAO/VBO/EBO、地表纹理、Blinn-Phong 光照和 wireframe；
- ImGui 面板显示 FPS、窗口尺寸、Height Map 尺寸、顶点数、三角形数、Draw Call，并支持调整 terrain size、height scale、wireframe 和光照参数；
- 当前仍是规则网格渲染，不包含 ROAM split/merge，自适应 LOD 从阶段 2 开始实现。

### 默认资源

```text
assets/heightmaps/Hm_Terrain_Test_129.pgm
assets/textures/Tex_Terrain_Debug_Diffuse.ppm
```

两个资源均为 stb 可加载的二进制 PNM 测试资源。后续可替换为同路径或代码中指定的 `png/jpg/tga/pgm/ppm` 资源。

## 阶段 2：基础 ROAM（Classic CPU 版）

### 目标

先完成可运行的 Classic CPU ROAM 原型，再补完符合经典 ROAM 语义的局部拓扑维护、diamond split、diamond merge 和裂缝约束。阶段 2 必须成为后续 Data-Oriented CPU 版和 GPU 版的可信 baseline。

### 子阶段

2A：二维二叉三角树可视化

- 不加载复杂高度；
- 使用平面或简单函数高度；
- 两个根三角形；
- 支持手动点击/按键 split；
- 显示每个节点 depth；
- 验证 child 的 domain triangle 正确。

2B：按距离细分

- 根据相机距离决定 split；
- 暂时仅 split，不 merge；
- 遍历 leaf triangle 并渲染；
- 观察近处细、远处粗。

2C：Height Map 细分

- 新顶点中点从 Height Map 采样；
- 将二维 domain triangle 转为三维 render triangle；
- 实现简单 geometric error；
- 使地形崎岖区域优先细分。

2D：邻接关系与裂缝处理

- Classic 节点使用裸指针表达 `parent`、`leftChild/rightChild`、`baseNeighbor/leftNeighbor/rightNeighbor`；
- split 前递归处理 `baseNeighbor`，保证当前 triangle 和 base neighbor 能组成合法 diamond；
- split 后立即按经典 ROAM 规则连接 child 的 `base/left/right neighbor`；
- split 后同步更新相邻 triangle 对当前节点的反向 neighbor 引用；
- 运行时路径不依赖全局 T-junction 扫描修裂缝；
- invariant checker 可离线扫描 active leaf，验证没有 T-junction、邻接互反关系正确；
- UI 提供 neighbor / diamond 传播统计，便于观察 forced split 成本。

2E：merge 与 hysteresis

- 引入 `splitThreshold > mergeThreshold`；
- merge 只能按完整 diamond 成对执行，不能只 merge 单侧 sibling；
- merge 前确认两个 sibling 都是 leaf，且对侧 diamond child 也满足回收约束；
- merge 后恢复父节点和周围 neighbor 的互相引用；
- 误差位于 split / merge 阈值之间时保持当前拓扑，防止频繁抖动；
- 记录 splitCount / forcedSplitCount / mergeCount / rejectedMergeCount。

### 验收标准

- 近处和地形变化大的区域明显细分；
- 远处保持粗网格；
- wireframe 可清晰展示 LOD；
- 默认运行路径不依赖全局 repair pass；
- invariant checker 验证无 T-junction；
- neighbor 指针满足互反关系和 diamond 约束；
- merge 不破坏裂缝约束；
- 能输出 active triangle count；
- 可作为三版本视觉一致性的 baseline。

### 当前实现状态（2026-08-03）

- `ClassicRoamMeshBuilder` 使用两个根三角形、裸指针 parent/child/neighbor 和持久化 binary triangle tree；
- 默认防裂缝路径是局部 `baseNeighbor` forced split；全局 validator 只检查并计数，不参与修复；
- 两个根各有一棵 nested wedgie tree，按二叉堆索引预计算到 `max(MaxDepth, sourceDepth)`，最细层为 0，父值严格执行论文公式 (1) `max(left,right)+abs(base midpoint displacement)`；
- `ComputeScreenErrorScore` 使用论文公式 (2)/(3) 对 nested thickness 做角点保守投影；完整 `ViewProjection`、drawable width/height 和 near-plane 特例共同决定像素 priority；
- 六个 inward frustum planes 用于方差扩张 AABB 测试；视锥外节点不主动 split，但仍允许 forced split 维持邻接约束；
- split 使用持久 indexed max-heap `Q_s`，保存全部 active leaves，并受默认 20,000 活动 leaf 硬预算限制；forced split 为调用链预留预算 token；
- merge 使用持久 canonical diamond min-heap `Q_m`；两队列在统一 crossover 循环中调度，满预算时以 merge-first 事务回收低损失区域并为高收益 closure 腾位；同一 Build 禁止刚 split 的 parent 立即 merge、刚 merge 的 parent 立即 split，避免非单调扩展 priority 造成事务振荡；
- `PathId`、split/merge 双阈值和最终 active path 共同提供跨 Build 迟滞；
- CPU Mesh 使用持久 dense slots：split 复用 parent 槽并追加一个 child，merge 写回 parent 并以末槽压缩消除空洞；只重写 dirty slots，每个 leaf 仍输出三个独立顶点；
- adapter 通过 borrowed Mesh + generation + dirty ranges 发布数据；OpenGL 用 `glBufferSubData` 部分上传，D3D12 为每个 frame slot 延迟消费 pending ranges，并在追赶多个 Build 时合并重叠区间；首次/reset/容量增长才全量上传；
- ImGui 已让 Classic、DOD 和 GPU ROAM-like 共用像素阈值与预算；Classic/DOD 都输出持久 `Q_s/Q_m`、资源交换次数和局部队列成员更新次数，Classic 另有 Mesh full/updated/reused/range 计数；
- Classic smoke 使用 6 个视点验证预算、视锥方向变化、单 Build 级联合并和 topology issue；`incremental-emit` 使用三次相同视点验证第三次 Build 零 dirty range；当前 OpenGL/D3D12 构建与 smoke 均通过；
- 本阶段采用工程等价口径：公式 (1)-(3)、连续 diamond 拓扑、局部队列 membership 与增量 Mesh 输出已经覆盖论文主要误差、拓扑和变化量更新效果；不继续复刻 triangle strips、优先级延期或完整最优性证明，每次 Build 全量刷新优先级的 `O(N)` 边界保留并明确记录；
- 阶段 2 的算法基线已封版。diamond/score heatmap 等更完整 debug draw 是后续可视化增强，不作为阶段完成阻塞项。

### 完整 Classic ROAM 完成记录

2F：关闭默认全局 repair

- 将全局 T-junction repair pass 从默认交互路径移除；
- 保留 invariant checker 作为 debug / test 工具，不参与每帧修复；
- UI 中将“裂缝修复”改为“经典局部约束”或类似名称，避免误导为全局 repair；
- 性能面板记录 update ms、split ms、merge ms、emit ms。

2G：建立经典拓扑不变量

- 每个节点明确保存 parent、leftChild、rightChild、baseNeighbor、leftNeighbor、rightNeighbor；
- root diamond 初始化后必须满足两个根互为 base neighbor；
- leaf split 前后都要保持 neighbor 指针互反；
- 内部节点不参与渲染 leaf 输出，但要保留 child 和 diamond 关系；
- 添加 `ValidateTopology()`，检查 dangling pointer、非互反 neighbor、非法 child 和 T-junction。

2H：完整 diamond split

- `SplitTriangle(node)` 只处理 leaf；
- 若 `node->BaseNeighbor` 存在且无法与当前节点直接组成 diamond，先递归 split base neighbor；
- 当前节点和 base neighbor 都满足 diamond 条件后再分裂；
- 分裂后按经典规则连接四个 child 的 neighbor；
- 分裂后更新左邻、右邻和 base neighbor child 对当前 child 的反向引用；
- forced split 只沿局部 neighbor 链传播，不扫描全局 leaf 集合。

2I：误差队列与 split 策略（已完成）

- Classic/DOD 共用 `RoamNestedWedgie.h`，按论文公式 (1) 预计算累计 thickness；
- 以像素为单位的 geometric bound 纳入完整投影、drawable width/height 和角点齐次分母极值；near-plane crossing 使用人工最大 priority；
- max heap 管理 split candidate，六平面视锥测试抑制不可见区域的主动细分；
- 最大深度、双阈值和活动三角形硬预算共同限制细分规模；
- 记录候选峰值、实际/forced split、普通拒绝和预算拒绝。

2J：完整 diamond merge（已完成）

- 使用动态最小堆管理 merge candidate；每次成功回收后重新检查 parent，支持单 Build 向上级联；
- 已限制 merge 只能回收 sibling leaf，不能只回收单侧 child；
- 若 base neighbor 也 split，则必须互为 base 并且两侧 child 都是 leaf，才允许成对 merge；
- merge 后会把外部 neighbor 指回 parent，并保持 diamond parent 互为 base neighbor；
- hysteresis 通过持久化拓扑、split / merge 双阈值和当前拓扑保持实现，不再只依赖路径 ID 假装 merge。

2K：验证与调试可视化（自动回归已完成，可视化可继续增强）

- 已增加拓扑验证开关，输出 active leaf 数、T-junction 数、非法 neighbor 数、最大深度和 validate 耗时；
- 已通过临时 probe 对比规则网格和 Classic ROAM 的高度范围、三角绕序、坐标范围、退化三角形和索引越界；
- wireframe 模式用于观察 Classic ROAM 细分结果；
- 已有按 leaf 状态/depth 着色和 forced split 高亮；diamond 对与 score heatmap 仍是可选增强；
- smoke 固定检查六个关键帧、预算上限和 topology validator。

2L：阶段 2 完成标准（已满足）

- 默认交互路径已无全局 `O(L^2)` repair；
- 开启 Classic ROAM 后，近处细分和远处 merge 会随相机位置变化；
- validator 在正式 smoke 的六个视点中报告 T-junction、invalid neighbor、invalid topology 均为 0；
- 相机同位置转向能回收视锥外细节，远距跳转能在单次 Build 级联合并；
- 活动 leaf 不超过 `TriangleBudget`，当前输出可作为对象式 CPU 正确性与性能基线。

## 阶段 3：Data-Oriented CPU 版本

### 目标

在相同 Height Map、相机路径、nested wedgie thickness、像素 SSE、视锥规则和活动三角形预算下，以数据导向方式重构 ROAM，验证 CPU 多核与数据布局收益。GPU ROAM-like 现已消费同一评分和预算输入，但仍是以 DOD 为持久拓扑真值的混合管线，具体比较边界见 `05-experiments-and-benchmarks.md`。

### 当前实现状态（2026-08-03）

- 两个根分别预计算 heap-indexed nested wedgie tree，节点用 `VarianceTreeIndex/VarianceIndex` 从 SoA 缓存公式 (1) thickness；预计算深度覆盖源分辨率且上限为 20；
- `ComputeScreenErrorScore` 与 Classic 共用公式 (2)/(3) CPU helper，消费完整 `ViewProjection`、drawable width/height 和六个 inward frustum planes，输出保守像素 bound 并抑制视锥外主动 split；
- `TriangleBudget` 默认 20,000，活动 leaf 每次 split 原子领取一个 token；并行 interior commit 与 forced split closure 共用同一硬上限；
- merge 保留安全 interior chunk 并行预提交，成功后把新满足条件的 parent 放回动态最小堆，可在同一 Build 向上级联；
- UI 对 Classic、DOD 和 GPU ROAM-like 显示同一组像素 split/merge 阈值和预算；renderer 在 FOV、朝向或 drawable 尺寸变化时触发三种 ROAM 重建；
- DOD 六视点 smoke 与 Classic 使用相同的预算、视锥、级联和拓扑正确性断言。

### 子阶段

3A：指针树转 Index-Based Node Pool

- 将 child / neighbor 从指针改为 index；
- 使用预分配 node pool；
- 避免运行期频繁 new/delete；
- 保留与 Classic 版一致的 split 语义。

当前状态：

- 已新增 `DataOrientedRoamPipeline`，节点池由 `std::vector` 预分配管理，parent / child / neighbor 统一使用 `NodeIndex`；
- 已通过统一 `ITerrainLodAlgorithm` 接口接入 benchmark，`--algorithm dod` 可运行 index-based 3A 版本；
- 3A 已作为 index-based baseline 保留在提交历史中，当前主线已进入 3B SoA 节点池实现。

3B：AoS 转 SoA

- 将 error、depth、flags、neighbor indices 分离；
- 只在需要时读取对应数组；
- 对齐 / padding 进行基本检查；
- 保证结果与 Classic 版一致或可解释地接近。

当前状态：

- `DataOrientedRoamNodePool` 已改为 SoA 数组，domain、parent/child、neighbor、error、方差索引、depth、build id 和 flag 分离存储；
- `ScreenErrors` 缓存最近一次 `Q_s` 优先级刷新结果；`ActiveLeafNodes` 同时作为活动叶索引和带反向索引的最大堆，`Q_m` 另以带反向索引的最小堆保存可合并 diamond，并保证每个 diamond 只入队一次；
- DOD 私有统计记录 SoA 数组数量和容量估算，统一 benchmark 接口保持不变。

3C：线程池与并行误差评估

- 实现轻量线程池或使用 EnTT/task scheduler；
- `ErrorEvaluationSystem` 并行；
- 记录单线程与多线程耗时；
- 每阶段记录 CPU 时间。

当前状态：

- 历史版本曾使用独立 DOD `ErrorEvaluation` pass，随后又将评分并入临时 split 候选扫描；当前实现改为并行刷新持久 `Q_s` 的全部优先级，再原地建堆；
- 自动 worker 模式会按硬件线程数保守封顶，小批量 leaf 保持串行以避免调度成本吞掉收益；
- `CpuErrorEvalMilliseconds` 与 `CpuBudgetLeafCollectMilliseconds` 在 DOD 中作为兼容字段保持为 0；`Q_s` 的优先级刷新、原地建堆和提交快照生成统一记录到 `CpuSplitCandidateMarkMilliseconds`，split 拓扑仍独立记录到 `CpuSplitTopologyMilliseconds`；
- Classic 与 DOD 的 `CpuSplitTopologySerialConvergenceMilliseconds` 现在采用同一范围：从 split 候选刷新结束到收敛循环结束，包含 split 拓扑提交和循环控制，但扣除收敛循环中为释放预算而执行的 merge；Classic 的 `SplitQueueTopologyMilliseconds` 仅保留为单次 `SplitNode` 提交的细粒度诊断。
- 统一 UI 和 benchmark 输出实际 worker 数、CPU 占用率及 `CPU split scan/mark ms`，该字段现在表示持久 `Q_s` 的优先级刷新、原地建堆和并行提交快照生成；
- topology commit、约束传播继续与只读扫描分开；安全 chunk 内候选可并行预提交，其余路径串行收敛。

3D：并行标记与收集

- 并行标记 split / merge candidate；
- thread-local buffer 收集 active leaves；
- 合并结果；
- 避免共享 vector 锁竞争。

当前状态：

- `ActiveLeafNodes` / `ActiveLeafNodePositions` 同时构成持久 `Q_s` 最大堆；`ActiveInternalNodes` 继续保存活动 internal 索引，持久 `Q_m` 最小堆只登记当前可 merge diamonds，并保证每个 diamond 只入队一次；
- 每个 Build 并行刷新 `Q_s/Q_m` 现有成员的像素 SSE/视锥优先级，再分别原地建堆；队列成员只由局部 split/merge 拓扑事务更新，不再每帧从活动集合重新发现；
- `Q_s.size()` 直接初始化剩余 triangle-budget token；预算满载时持续比较 `max(Q_s)` 与 `min(Q_m)`，只要最高 split 收益仍大于最低 merge 损失，就执行 merge-first 资源交换，直到队首条件收敛；
- threshold merge 和初始 split 快照仍可按安全 interior chunk 并行预提交；worker 运行前统一移除受影响的 `Q_m` 邻域，join 后由主线程提交活动索引与队列成员变化，再恢复局部 diamond；
- validator 仍从 root 独立遍历，并额外校验两个 heap 的顺序、反向 position、diamond 唯一代表关系和 `Q_m` 成员完整性，防止持久队列自证正确；
- split/merge 已同步维护 `ActiveLeafNodes`；最终 mesh emit、统计和 GPU snapshot 直接读取这份拓扑稳定后的只读输出视图，不再从 root 递归收集，也不再复制第二份 leaf vector。DOD 的 `CpuFinalLeafCollectMilliseconds` 因而为 0；validator 仍保留独立 root traversal，用于交叉校验活动索引而不是让索引自证正确。

2026-08-01 同参数隔离 A/B 中，完整 node-pool 扫描与 active-internal 扫描都保留索引维护成本，只切换 merge 候选来源。DOD 的 `CpuMergeCandidateMarkMilliseconds` 从 `1.6273 ms` 降至 `0.9777 ms`，完整 Merge pass 从 `1.6495 ms` 降至 `0.9985 ms`；两组最大拓扑错误均为 0，平均 triangles/nodes 差异低于 1%。单轮数据用于确认优化方向，正式结论仍应采用多轮重复实验。

同日 split 融合实验先验证了一个反例：只合并循环、仍扫描完整历史 node pool 并沿 parent 链判断 active 时，原三个阶段之和从 `1.6152 ms` 升至 `1.6692 ms`。加入增量 active leaf 索引后，融合 `Split scan/mark` 降至 `0.2969 ms`，Split pass 从 `1.3832 ms` 降至 `0.3044 ms`，CPU update 从 `5.3143 ms` 降至 `3.6372 ms`；最大拓扑错误仍为 0。说明收益来自消除历史节点/parent-chain 工作和重复 leaf 遍历，而不是简单把计时字段合并。

3E：拓扑提交策略

- 初版保留单线程 topology commit；
- 可选：按 Terrain Chunk 分区处理；
- 可选：边界采用 skirt 或边界约束，降低跨 chunk 同步复杂度。

当前状态：

- DOD topology commit 已加入固定 `8x8` terrain chunk 分区，只有完整落在同一 chunk 的候选会进入并发提交；
- split 并发提交只处理已有 child 可复用、且不会触发 forced split 的内部候选，fresh child 分配和跨 chunk 邻接继续串行回退；
- merge 并发提交只处理影响节点全集都在同一 chunk 内的候选，diamond merge 跨 chunk 时仍由串行路径保证 neighbor 一致性；
- 2026-08-01 配对实验表明 Merge chunk commit 在约 160 个 interior candidates 后才稳定获益；因此 Merge 默认阈值已取 160。Split 现有场景只有 2-17 个安全 candidates 且并行总体更慢，暂时保留独立的 32 阈值；
- 所有 split 路径共用原子预算 token；并行提交不会让活动 leaf 超过 `TriangleBudget`，forced chain 失败会归还尚未提交的 caller token；
- 并行 merge 返回新可检查的父节点，主线程动态最小堆继续级联，避免父层固定等待下一个 Build；
- 统一 `CpuWorkerCount` 已纳入 topology commit worker 数；merge 与 split 拓扑提交分别记录在 `CpuMergeTopologyMilliseconds` 与 `CpuSplitTopologyMilliseconds`，不再折叠为单一 topology 阶段。

### 验收标准

- 视觉结果与 Classic 版相同或接近；
- 在中等/大规模场景下更新耗时优于 Classic 版；
- 有可展示的阶段时间分解；
- 能说明哪些环节并行收益高、哪些环节被拓扑依赖限制。

## 阶段 4：GPU ROAM-like 版本

### 目标

在不破坏 Classic / DOD 结果可比性的前提下，把最适合 GPU 批处理的 ROAM 阶段逐步迁移到 GPU。GPU 版优先证明“误差评估、候选标记、active leaf 收集、mesh emit / draw submit”这些高并行环节的收益，拓扑 split / merge 作为后续冲刺，不阻塞主报告。

GPU 版不强求原样复刻 Classic 的全局优先队列。推荐项目表述为：保留二叉三角域、视点相关 screen error、split / merge 阈值和无裂缝约束思想，用 GPU-friendly 的批量阈值决策与分块提交替代严格串行队列。

### 历史前置条件（2026-07-06）

- 已有统一 `ITerrainLodAlgorithm`、`TerrainLodRenderPacket` 和 `TerrainLodStats`，枚举中已预留 `GpuRoamLike`；
- `TerrainLodRenderPacket` 已预留 `GpuBuffers`、`GpuIndirect`、GPU buffer id 和 indirect draw buffer 字段；
- 当前 `TerrainRenderer` 仍只消费 CPU mesh，GPU-only packet 分支尚未实现；
- Classic 与 DOD 已在 smoke benchmark 中保持相同三角形数和拓扑统计，可作为 GPU 版行为对照；
- DOD 已具备 SoA node pool、active leaf 快照、chunk id 缓存、并行 candidate marking 和保守 chunk topology commit，是 GPU buffer schema 的主要参考；
- Benchmark CSV 已包含八个 GPU 算法 pass、`gpuPassSumMs`、`cpuGpuUploadBytes`、`cpuGpuReadbackBytes`、CPU worker 和 CPU 利用率字段；
- 当前 macOS 测试环境 OpenGL 运行时报告为 4.1，Compute Shader 需要 OpenGL 4.3 或对应扩展，因此必须先实现 GPU capability gate，无法运行 compute 时 benchmark 应明确 skip 而不是失败。

### 分级交付

```text
Level A：GPU adapter + buffer schema + capability gate
Level B：GPU error evaluation + candidate marking，CPU topology commit
Level C：GPU active leaf compaction + GPU mesh emit / GPU buffer rendering
Level D：GPU indirect draw，尽量减少 CPU readback
Level E：GPU split-only 或 split/merge topology update
```

主线目标建议定为 Level C；Level D 是强展示项；Level E 是冲刺项。若目标机器没有 compute shader 能力，仍应完成 Level A，并让 benchmark / UI 能清晰说明 GPU 路径不可用原因。

### 子阶段

4A：GPU 能力检测与算法壳

- 新增 `GpuRoamLike` 算法适配器，接入统一 `ITerrainLodAlgorithm`；
- 在 renderer / benchmark 中允许选择 GPU 版，但当 OpenGL compute 不可用时返回明确 skip / error message；
- 抽出 GPU capability 查询，记录 OpenGL version、compute shader、SSBO、atomic counter、indirect draw、timer query 支持情况；
- 建立 GPU shader / buffer 资源生命周期规范，避免算法层直接散落 OpenGL 对象管理；
- 接入 OpenGL timer query；当前实现为八个非嵌套 query，分别写入 split 前 leaf collection、leaf error/frustum、split candidate、merge candidate score、split/direct-diamond commit、leaf reset、split 后 leaf collection 和 mesh emit 字段；
- 暂不改变地形输出，目标是让三版本入口和失败语义稳定。

验收标准：

- `--algorithm all` 在 GPU 不可用时稳定跳过 GPU，不影响 Classic / DOD benchmark；
- UI 可以显示 GPU 路径不可用原因；
- `GpuRoamLike` 的 `Info()`、`Capabilities()`、`Stats()` 和 `Reset()` 路径完整；
- 不支持 compute 的机器上也能通过 smoke test。

阶段完成记录：

- 已新增 `GpuRoamLike` 算法壳并接入 renderer、UI 下拉框和 CLI benchmark；
- 已新增 OpenGL GPU capability 查询，记录 context、OpenGL version、renderer、compute shader、SSBO、atomic counter、indirect draw 和 timer query 能力；
- 无窗口 benchmark 没有 OpenGL context 时，`--algorithm all` 会把 GPU 明确标为 skip，`--algorithm gpu` 会返回失败并输出原因；
- macOS OpenGL 4.1 环境下，UI 选择 GPU ROAM-like 时会显示 OpenGL 4.3 / compute shader 不可用原因，不会静默回退成 CPU 成绩；
- 运行时 benchmark CSV 和汇总表输出八个 GPU pass、pass sum、CPU-GPU upload bytes 和 readback bytes；
- 4A 完成时 GPU 版处于 Level A，尚未执行 compute shader、GPU mesh emit 或 GPU buffer rendering；后续 4C-4G 已补齐这些路径。

4B：GPU buffer schema 与 DOD 快照对齐

- 以 DOD SoA 字段为基准定义 GPU node buffer 布局；
- 明确 std430 对齐规则，避免 CPU struct padding 和 shader 端读取不一致；
- 将 domain、parent / child、neighbor、depth、error、screenError、flags、pathId、chunkId 拆成 GPU-friendly buffer；
- 上传 Height Map texture、camera/settings UBO 和 active leaf index buffer；
- 增加 `CpuGpuUploadBytes` 统计，区分 full upload 与 dirty range upload；
- 先只上传数据，不在 GPU 上修改拓扑。

验收标准：

- GPU buffer 中 node 数、active leaf 数、max depth 与 DOD stats 对齐；
- 上传字节数进入 benchmark CSV；
- 支持 debug readback 少量 node 做字段一致性抽样；
- 不引入额外 UI 参数，沿用三版本统一核心参数。

阶段完成记录：

- 已定义 `GpuRoamNodeRecord`，按 16 字节组打包 domain、geometric/screen error、parent/child/neighbor、chunk、flag、path id、build id 和 depth；
- 已定义 `GpuRoamBufferSnapshot`，从 DOD `DataOrientedRoamState` 导出 node buffer 与 active leaf index buffer；
- DOD builder 已提供只读 `State()` 快照入口，GPU 模块读取快照但不修改 DOD 拓扑；
- GPU ROAM-like 在 OpenGL 4.3 可用时会先运行 DOD CPU topology，随后上传 node SSBO、active leaf SSBO 和 R32F height map texture；
- `TerrainLodRenderPacket` 已补充 GPU node buffer、height map texture、active leaf count 和 status message 字段；
- 4B 阶段最初使用 CPU mesh fallback 验证 GPU staging，后续 4F 已将 GPU 路径推进到 GPU buffer / indirect draw 输出；
- 本机 macOS OpenGL 4.1 环境无法执行 SSBO 上传路径，只验证了 build、smoke 和无窗口 benchmark 的 GPU skip 语义。

4C：GPU Error Evaluation

- Compute shader 读取 height map texture、node buffer、camera/settings UBO；
- 对 active leaf 使用快照携带的 nested wedgie `GeometricError`，按 View、Projection 和 drawable height 计算像素 SSE，并以 thickness 扩张 AABB 做六平面视锥测试；
- 输出 `screenError` buffer；
- CPU 只抽样 readback 少量 error 值做验证，默认 benchmark 不全量 readback；
- 用 timer query 记录 compute 时间，用 CPU 计时记录 upload / readback 时间。

验收标准：

- 抽样 GPU error 与 DOD 的共享像素 SSE 公式误差在可解释范围内；
- CSV 同时记录 CPU 互斥算法阶段、八个 GPU pass、upload/readback bytes；
- GPU 不可用时该阶段保持 skip，不破坏 CPU benchmark；
- 文档记录浮点误差、采样方式和 OpenGL 版本要求。

阶段完成记录：

- 已新增 GPU error evaluation compute shader，读取 R32F height map texture、node SSBO 和 GPU compact 后的 active leaf buffer；
- OpenGL GLSL 与 D3D12 HLSL 都读取 DOD snapshot 中按公式 (1) 传播的 `GeometricError`，并使用 CPU 兼容的显式双线性高度采样、像素 SSE 和六平面视锥测试写入 screen error buffer；
- GPU 算法链已按实际 shader 责任拆成八个 OpenGL timer query，并通过延迟槽位整体回读；split 与 merge candidate 扫描不再混在一个计时区间；
- 默认只 readback 少量 active leaf 和 error 样本，避免全量 screen error 回读污染性能口径；
- leaf error evaluation 驱动 GPU split candidate；merge candidate pass 单独对 split parent 重新评分，但只产生诊断列表。GPU split-only 只修改当前 GPU 快照，不反写 CPU DOD 持久拓扑，merge commit 仍由 CPU DOD 完成。

4D：GPU Split/Merge Candidate Marking 与候选压缩

- Compute shader 根据 `screenError`、split / merge 阈值、depth 和 active 状态写 split / merge flag；
- 使用 atomic append 或 prefix-sum compact 输出 split candidate list、merge candidate list；
- CPU topology commit 暂时继续复用 DOD 保守提交策略，GPU 只负责大批量标记；
- readback 只读取 compact 后候选列表和计数，不读取全量 flag；
- 对比 DOD candidate marking 的 CPU collect / mark 耗时。

验收标准：

- GPU 候选数量与 DOD 在 smoke profile 下保持一致或差异可解释；
- CPU readback bytes 明确低于全量 node buffer；
- topology commit 后 active triangle count 与 Classic / DOD 对齐；
- benchmark 可展示 CPU collect / mark 时间下降，或说明瓶颈转移到 readback / topology commit。

阶段完成记录：

- 已将 GPU candidate shader 拆为 split active-leaf classification 与 merge parent scoring 两次 dispatch，分别生成候选 buffer 和独立计时；
- merge candidate 目前作为 shadow 预筛选，扫描 split node 并按 merge threshold 标记候选，不参与真正拓扑提交；
- split / merge candidate count 通过小 counter buffer readback，用于后续和 DOD candidate marking 对齐；
- 4D 完成时 CPU topology commit 仍完全由 DOD builder 负责，GPU candidate list 还不改变三角形结果。

4E：GPU Active Leaf Compaction

- GPU 遍历 node buffer，压缩 active leaf index；
- active leaf buffer 成为 leaf error evaluation、split candidate marking 和 mesh emit 的共享输入；GPU merge candidate scoring 仍扫描 split parent 节点池，不复用 CPU DOD 的持久 `Q_m`；
- 默认只 readback active leaf count 和必要统计；
- 继续用 CPU topology commit，保证风险集中在收集和数据流上。

验收标准：

- active leaf count 与 DOD `ActiveLeafNodes` 输出视图一致；
- CPU collect/mark 与 GPU split 前 leaf collection、split candidate、merge candidate score 分开记录，不能用一个笼统 GPU 时间替代；
- readback 口径清楚，只读计数时不影响主要性能结论；
- debug 模式可选择全量 readback 以定位错误，但 benchmark 默认关闭。

阶段完成记录：

- 已新增 GPU active leaf compaction compute shader，从 node flag 扫描 active leaf 并写入 active leaf SSBO；
- CPU 上传的 DOD active leaf list 已改为 GPU 端重新压缩，CPU 只保留 expected active leaf count 做校验；
- compaction 后 readback counter，并与 DOD `ActiveLeafNodes` 数量对齐，不一致时直接失败；
- 本机 macOS OpenGL 4.1 无法执行该 compute path，只验证了构建和 GPU skip 语义。

4F：GPU Mesh Emit 与 `GpuBuffers` 渲染分支

- Compute shader 根据 active leaf buffer 生成 GPU vertex / index buffer；
- `TerrainRenderer` 支持消费 `TerrainLodRenderMode::GpuBuffers`，不再要求 `CpuMesh` 非空；
- 顶点 position、normal、uv、debug color 与 CPU emit 对齐；
- 保留 CPU mesh fallback，便于 GPU 输出错误时快速回退对照；
- 统计 `CpuMeshBuildMilliseconds`、`CpuUploadMilliseconds` 和 `CpuGpuUploadBytes` 的下降。

验收标准：

- GPU buffer 渲染画面与 DOD CPU mesh 视觉一致；
- smoke benchmark 中 active triangle count 与 CPU 版本一致；
- CPU mesh build 和 CPU-GPU mesh upload 在 GPU 路径中接近 0 或只剩 fallback/debug 成本；
- renderer 对 `CpuMesh` 为空但 GPU buffer 有效的 packet 不再报错。

阶段完成记录：

- 已新增 DOD topology-only 更新入口，GPU ROAM-like 复用 DOD 拓扑提交但不再生成 CPU mesh；
- 已新增 GPU mesh emit compute shader，根据 GPU active leaf buffer 写出 terrain vertex buffer、index buffer 和 draw command；
- GPU mesh emit 的顶点布局按 `TerrainMeshVertex` 的 13 个 float 槽位写入，避免 std430 `vec3` padding 与 C++ layout 不一致；
- `TerrainRenderer` 已支持 `TerrainLodRenderMode::GpuBuffers`，可在 `CpuMesh` 为空时绑定算法输出的 GPU vertex / index buffer 绘制；
- benchmark 校验已识别 GPU-only packet，不再要求 GPU 路径提供 CPU mesh；
- 当前 GPU 路径以 CPU DOD 拓扑为安全 baseline，并额外接入 GPU split-only 实验层；GPU 负责 split 前/后 leaf collection、leaf error/frustum、split candidate、诊断性 merge candidate score、direct-diamond split commit 和 mesh emit，完整 merge 与递归 forced-split chain 尚未迁移。

4G：GPU Indirect Draw

- 在 GPU 端生成 indirect draw command；
- `TerrainRenderer` 支持 `TerrainLodRenderMode::GpuIndirect`；
- CPU 只提交一次 indirect draw，不读取完整 index count；
- 保留逐 pass timer query，区分八段 GPU shader 工作和 render draw 时间。

验收标准：

- `DrawElementsIndirect` 或等价路径可以绘制 active leaf mesh；
- CPU readback 只保留统计所需最小数据；
- `RenderMilliseconds` 与八个 GPU pass 及其 `GpuPassSumMilliseconds` 分开记录；
- GPU unavailable 或 indirect draw unsupported 时可回退 `GpuBuffers`。

阶段完成记录：

- GPU mesh emit pass 会在 GPU 端写出 `DrawElementsIndirect` command；
- `TerrainRenderer` 已支持 `TerrainLodRenderMode::GpuIndirect`，GPU 支持 indirect draw 时走 `glDrawElementsIndirect`，否则回退 `GpuBuffers`；
- CPU 不回读完整 vertex / index buffer，packet 的 index count 仅由 active leaf count 推导用于统计和防御检查；
- 运行时 benchmark 已按 Classic、Data-Oriented、GPU 顺序执行同一条相机路径，报告记录 GPU device、compute 时间、上传/回读字节和 capability skip 原因；
- 新增 `--runtime-benchmark` 自动入口，三种算法完成后写出 Markdown / CSV 并退出，任一算法重建失败时返回非零；
- macOS OpenGL 4.1 环境仍按 capability gate 跳过 GPU；Windows RTX 5090 / OpenGL 4.3 已完成三算法 runtime benchmark 实机验证。

4H：GPU Split-Only Topology Update（冲刺）

- 在 GPU 端从 split candidate 批量提交 split-only；
- 采用固定容量 node pool 和 atomic allocation，暂不回收节点；
- 只处理 chunk interior candidate，跨 chunk / forced split / fresh dependency 先回退 CPU 或延后；
- 使用多轮 compute pass 传播必要的局部约束，限制最大迭代次数；
- CPU validator 可通过 debug readback 验证 T-junction、neighbor 和 max depth。

验收标准：

- split-only 场景下 active triangle count 能随相机接近增加；
- 不支持 merge 时必须在 UI / benchmark 中明确标注能力边界；
- validator 在固定 smoke 场景中无 T-junction 和 invalid neighbor；
- 若出现约束无法收敛，必须记录失败案例和回退策略。

阶段完成记录：

- 已新增 `GpuRoamSplitOnlyTopology` 独立 pass 文件，将 GPU 拓扑扩展逻辑从 adapter 中拆出；
- GPU node/leaf buffer 只按 `TriangleBudget - CPU快照leaf数` 允许的额外 split 容量预留；split-only pass 通过 `allocatedNodeCount` atomic counter 分配 child record；
- split-only pass 支持两类保守提交：外边界 base edge 单 triangle split，以及互为 base neighbor 且同 chunk 的 diamond pair split；
- 所有 invocation 共享 `remainingSplitBudget` 原子 token；边界 split 消费 1，diamond pair 消费 2，parent claim 或 child allocation 失败时回滚 token，预算拒绝写入独立 counter；
- split-only pass 暂不回收节点、不执行 merge，也不把 GPU 生成的拓扑写回 CPU DOD state；
- mesh emit 不再依赖 CPU 传入的 active leaf 数，而是读取 GPU counter 中重新 compaction 后的最终 active leaf count；
- OpenGL 与 D3D12 的应用级 GPU smoke 都检查 packet 非空、最终三角形不超共享预算，以及 CPU DOD 持久拓扑的三类 issue 为零；OpenGL 延迟 counter readback 额外检查 active/node 计数和 token 守恒；
- 当前 GPU split 只接受外边界或同 chunk 互为 base 的直接 diamond，不实现跨 chunk forced closure；新 child 的 `GeometricError` 为 0 且不会跨帧持久化。这是混合实验层的明确能力边界，仍需 GPU 几何 readback/离线图像验证才能完整证明额外 split 后无裂缝。

4I：GPU Split / Merge Topology Update（可选冲刺）

- 在 GPU 端增加 merge candidate 和 sibling / diamond 回收；
- 引入 node recycle 或 free list，评估回收成本；
- 处理 base neighbor、left/right neighbor 的互反更新；
- 支持 chunk boundary 的串行回退或边界约束；
- 对比 CPU DOD topology commit，说明 GPU 化是否值得继续推进。

验收标准：

- split / merge 都能在固定相机路径中稳定运行；
- active triangle、split、merge、invalid topology 与 DOD 对齐或差异可解释；
- 每个失败案例都有可复现 benchmark profile 和 debug readback 记录；
- 若成本高于 CPU DOD，应在报告中明确说明拓扑依赖是瓶颈。

### 验收标准

- 最低可交付：完成 4A 到 4D，GPU 至少承担 error evaluation 和 candidate marking，CPU topology commit 仍可保留；
- 推荐可交付：完成 4A 到 4F，GPU 路径能输出 GPU buffer 并减少 CPU mesh build / upload；
- 强展示项：完成 4G，使用 indirect draw 进一步降低 CPU 提交成本；
- 冲刺项：完成 4H 或 4I，尝试 GPU topology update 并记录局限；
- 三版本 benchmark 必须使用同一 Height Map、相机路径、核心参数和 CSV 字段；
- GPU 不可用、compute 不可用、indirect draw 不可用都必须被清晰标注为 skip / fallback，不允许静默退回 CPU 后当作 GPU 成绩；
- 所有阶段都要记录 CPU-GPU upload/readback bytes，避免 readback 成本掩盖 compute 收益；
- 任何 GPU 结果和 DOD/Classic 输出不一致时，都按 bug 处理，修复后写入 bug 修复记录。

## 阶段 5：实验、可视化与报告材料整理

状态：已完成（2026-07-08）。课程阶段报告和实验数据已冻结到历史文档；后续研究变体必须建立独立算法标识和统计口径。

### 目标

将“能跑”变成“能证明”。

### 任务

- 实现核心 Debug View；
- 固定相机路径回放；
- CSV 统计导出；
- 绘制性能曲线；
- 整理截图、视频和报告图表；
- 记录失败案例与降级原因。

### 验收标准

- 至少输出 2 到 3 张核心性能图；
- 至少输出 4 到 6 张 Debug / 最终画面截图；
- 有完整 CSV 日志；
- 结论基于数据，不只凭主观观感。
