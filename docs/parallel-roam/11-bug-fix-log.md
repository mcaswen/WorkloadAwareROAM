# Bug Fix Log

本文档记录 Parallel ROAM 开发过程中已经完成确认的 bug、易误判现象、定位过程、解决方案和验证方式。正在调查、尚未修复或用户未确认修复完成的问题，不写入正式记录。

> 各条记录保留问题发生时的实现语境；较早条目中的“当前”“后续”和旧 benchmark 数值不代表仓库现状。算法质量基线见 BUG-019 至 BUG-021；最新性能统计口径见 BUG-023 和 `docs/source_analysis/classic_roam_context.md`。

## 记录规范

每条记录应包含：

- `状态`：`Open`、`Mitigated`、`Fixed`、`Won't Fix`、`Non-bug`
- `严重级别`：`高`、`中`、`低`
- `发生阶段`：bug 出现时所在的里程碑阶段、子阶段或功能分支
- `现象`：用户能观察到的表现
- `定位`：原因、相关模块和关键代码路径
- `Debug 过程`：关键假设、验证命令、探针结果、被排除的错误方向
- `解决方案`：已经实施的修复，或计划采用的修复
- `验证`：验证命令、数据、截图或人工检查方式
- `后续`：仍需补的测试、profiling 或架构调整

正式记录规则：

- 默认等用户明确确认“修复完了”之后再记录
- 不把调查中、尚未验证或刚发现的问题提前写成正式 bug log
- 每轮构建、smoke、benchmark 或回归测试后，如果结果与本轮预期不符且随后已修复，应作为 bug 记录
- 注释覆盖率、阶段标签、中文标点、连续注释块等规范门禁修复不算 bug，不写入本日志
- 如果中途出现错误判断，最终记录中要说明该方向为什么被排除
- 定位和解决方案不能只写结论，应记录关键代码路径、触发条件、为什么旧逻辑失败、为什么新逻辑能覆盖该场景
- 每条记录都要写清楚发生阶段，尤其是阶段性实现过程中引入的回归，便于回看里程碑风险
- 性能 bug 必须写清楚 Debug / RelWithDebInfo / Release 构建类型、关键参数和场景规模

## 已修复问题记录

### BUG-001：RelWithDebInfo preset 构建成 bootstrap

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 0 工程初始化与依赖接入，CMake preset 和 FetchContent 路径配置期间
- 现象：执行 `cmake --preset relwithdebinfo` 和 `cmake --build --preset relwithdebinfo` 后，程序只输出 `Parallel ROAM bootstrap`，GLM、stb、Dear ImGui 未链接，完整 app 没有启用
- 定位：默认 `relwithdebinfo` preset 没有开启 `PARALLEL_ROAM_FETCH_MISSING_DEPS`，本机又没有完整安装 GLM、stb、Dear ImGui，因此 CMake 进入 bootstrap fallback
- 解决方案：新增 `relwithdebinfo-fetch` 和 `release-fetch` preset，并添加 `scripts/run_relwithdebinfo_fetch.sh`、`scripts/run_release_fetch.sh`、`scripts/run_debug_fetch.sh`、`scripts/run_smoke_test_fetch.sh` 作为快捷入口
- 验证：`cmake --list-presets` 能列出 `relwithdebinfo-fetch` 和 `release-fetch`；`./scripts/run_smoke_test_fetch.sh` 能构建完整 app 并通过 smoke test
- 后续：若后续引入 vcpkg 为主路径，应补充 `relwithdebinfo-vcpkg` 脚本

### BUG-002：`--smoke-test` 下窗口闪退

- 状态：`Non-bug`
- 严重级别：`低`
- 发生阶段：阶段 0 工程初始化与最小渲染闭环，smoke test 自动化验证入口接入期间
- 现象：执行 `./build/debug-fetch/bin/ParallelROAM --smoke-test` 时，窗口出现一下就马上退出
- 定位：`--smoke-test` 是自动验证入口，目标是创建窗口、加载 OpenGL、渲染数帧后自动退出，用于 CI 或快速环境检查
- 解决方案：运行交互版本时不要带 `--smoke-test`；快捷脚本中 `run_smoke_test_fetch` 专门保留自动退出行为
- 验证：不带 `--smoke-test` 运行时窗口会持续打开；带 `--smoke-test` 时命令行输出 OpenGL renderer 和 version 后正常退出
- 后续：可以在启动日志中明确打印 `smoke test will exit automatically`，减少误判

### BUG-003：VSCode / clangd 报项目头文件找不到

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 0 工程初始化，VSCode CMake Tools、clangd 和 `compile_commands.json` 配置期间
- 现象：编辑器报 `'app/CameraController.h' file not found clang(pp_file_not_found)`，但命令行构建可以通过
- 定位：clangd 没有读取到当前 build preset 生成的 `compile_commands.json`，因此不知道 `src/` include path 和 FetchContent 依赖 include path
- 解决方案：保证使用带完整依赖的 CMake preset 配置工程，并让 VSCode CMake Tools / clangd 使用对应 build 目录的 `compile_commands.json`
- 验证：重新配置 `debug-fetch` 或 `relwithdebinfo-fetch` 后，编辑器红线消失，命令行构建仍通过
- 后续：如红线再次出现，应检查 VSCode 当前选中的 CMake preset 和 clangd compile commands 路径

### BUG-004：开启 Classic ROAM 裂缝修复后仍产生 T-junction

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2D 邻接关系与裂缝处理早期实现期间
- 现象：开启裂缝修复后探针仍能发现 T-junction，`center-default`、`near-corner`、`far` 场景都有残留裂缝候选
- 定位：早期 split 只处理当前节点的 `BaseNeighbor`，最终 leaf 邻接重建也只匹配完整共享边，无法发现“一条粗边被多条细边贴住”的情况
- 解决方案：Classic 节点改为裸指针拓扑，接入基于 `baseNeighbor` 的 diamond forced split 传播，并加入 T-junction repair pass 扫描粗细边贴合情况后继续 split
- 验证：使用临时探针检查多个相机位置的 T-junction 候选；UI 中可观察 forced split、constraint propagation 和 crack risk 统计
- 后续：当前 repair pass 偏全局扫描，后续应替换为边哈希或局部 diamond repair，避免性能问题

### BUG-005：Classic ROAM 的 PathId 撞号

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2E merge / hysteresis 历史状态统计接入期间
- 现象：rootA 从 `1` 开始，rootB 从 `2` 开始，而 child 使用 `parentPathId * 2`，导致 rootA 的左 child 与 rootB 根节点同为 `2`
- 定位：两棵 root tree 的 path namespace 没有隔离，hysteresis 和 merge 统计会因为 ID 碰撞被污染
- 解决方案：将两棵 root tree 的 `PathId` 分区，rootA 使用低位起点，rootB 使用高位起点，child 仍保持 binary heap 风格派生
- 验证：检查 root 和 child path 生成逻辑，确认两棵 root tree 的子树不会落入同一 ID 区间
- 后续：后续 Data-Oriented 版本如果改用 index pool，应保留稳定路径 ID 或显式历史表

### BUG-006：ROAM 输出三角绕序反向

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 2 Classic CPU ROAM，2C leaf triangle 输出到 render mesh 期间
- 现象：探针显示 ROAM 输出三角在 XZ 投影中与规则网格绕序相反；当前没开启 face culling 时不明显，但后续法线 debug 或背面剔除会出错
- 定位：ROAM domain triangle 的三维 emit 顺序没有统一到正 Y 方向
- 解决方案：在输出 render triangle 时检查三角法线方向，若法线不是正 Y 则交换顶点顺序
- 验证：探针检查 ROAM 三角绕序；规则网格 baseline 与 Classic ROAM 的面方向一致
- 后续：加入 face culling debug 开关时应再次验证

### BUG-007：Classic ROAM 近距离细分变化很小

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2B split / 2C geometric error 和 screen error 估算期间
- 现象：打开 Classic ROAM 后，相机靠近地形时三角形数量变化很小，近处仍然没有明显细分
- 定位：split score 主要依赖 height error，平坦区域或 base 中点误差较低时，即使相机很近也缺少屏幕空间细分压力
- 解决方案：几何误差改为采样三条边中点和重心，并在 screen error score 中加入近距投影边长权重，同时提高默认 `MaxDepth`
- 验证：右侧 UI 中 active triangle、actual depth 和 split count 会随相机靠近更明显变化；wireframe 可观察近处网格密度提升
- 后续：后续应引入真正的屏幕空间误差投影，而不是当前经验权重

### BUG-008：FPS 最低只显示 10

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 1 UI 面板和运行时统计接入，真实帧时间显示拆分期间
- 现象：UI 中 FPS 低到一定程度后始终显示 10，无法判断真实卡顿程度
- 定位：主循环原先将 delta time clamp 到 `0.1s` 后同时用于相机更新和 FPS 计算，导致显示 FPS 下限被固定为 `1 / 0.1 = 10`
- 解决方案：拆分 raw delta 和 clamped delta；相机移动使用 clamped delta，FPS 和 frame ms 使用 raw delta
- 验证：UI 显示真实 frame ms；当单帧超过 100ms 时 FPS 不再被钳制到 10
- 后续：需要补 profiling 面板，将算法重建、GPU 上传、绘制分开统计

### BUG-009：Classic ROAM 单帧耗时达到 4500ms

- 状态：`Mitigated`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，早期全局 crack repair 路径
- 现象：开启 Classic ROAM 后一帧耗时可达到 4500ms 左右，交互几乎不可用
- 定位：Debug 构建下 `RepairCracksWithDiamondSplits` 和 `FindCrackRepairCandidates` 进行全局 leaf 对 leaf 扫描，主要复杂度接近 `O(P * L^2)`；`RebuildLeafNeighborLinks` 也有 `O(L^2)` 成本
- 解决方案：已新增 `relwithdebinfo-fetch` / `release-fetch` 快捷脚本，性能观察默认使用优化构建；渲染器已加入相机位移阈值缓存，避免静止或微小移动时每帧重建 Classic ROAM mesh
- 验证：临时 benchmark 显示 Debug 下高深度 crack repair 是主要瓶颈；优化构建耗时显著下降但全局扫描结构仍然存在
- 后续：需要用边哈希、空间哈希或局部 diamond repair 替换全局 T-junction 扫描；同时在 UI 中加入 build ms、repair pass、candidate count 等 profiling 指标

### BUG-010：持久化拓扑后相机移动不再继续细分

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM，2J-2L 持久化拓扑、严格 diamond merge、拓扑验证入口接入之后
- 现象：完成 2J-2L 修改后，开启 Classic ROAM 时地形 LOD 表现几乎不再随相机变化；用户靠近地形、移动到不同位置后，wireframe 和地形三角形密度看起来没有继续更新；UI 中 `Split` 长时间为 `0`，容易误判为只是统计口径变化，但实际视觉上也没有新的细分发生
- 定位：根因在 `src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp` 的 `RefineWithSplitQueue`。2J-2L 把 Classic ROAM 从“每次 build 清空并从 root 重新细分”改成“持久化拓扑，普通相机移动复用已有 bintree”，但 split priority queue 的入口仍然只调用 `enqueueCandidate(rootA)` 和 `enqueueCandidate(rootB)`。`enqueueCandidate` 只接受 active leaf；第一轮构建后 root 已经 `IsSplit == true`，不再是 leaf，于是第二轮之后 root 会被直接拒绝，队列为空，当前 active leaf 根本没有机会按新相机位置重新计算 screen error。换句话说，持久化拓扑改变了 split 的入口集合，但候选队列仍按临时拓扑时代的 root-only 入口设计
- Debug 过程：最初根据 UI 中 `SplitCount == 0` 判断，怀疑是统计语义问题，因为持久拓扑后 `SplitCount` 表示“本次 build 新发生的 split”，不等于“当前拓扑里仍然展开的 split”。因此先补充 `ActiveSplitCount`，把 UI 拆成“活跃 Split”和“本帧 Split”。用户随后确认不是单纯 UI 问题，而是地形视觉真的不继续细分。继续检查渲染链路后排除了线框绘制模式、OpenGL draw call、mesh upload 和高度图资源路径：规则网格模式正常，Classic ROAM 初始 mesh 能生成，问题只发生在已有拓扑随相机继续更新的阶段。随后回到算法入口检查 `RefineWithSplitQueue`，发现队列只从 root 入队，而 root 在持久拓扑中通常已经是 internal node。该结论也解释了为什么 2J 之前不明显：旧实现每帧重建整棵树，root 每次都是 leaf，可以进入队列；持久化后 root 只在第一轮是 leaf，后续 build 的候选集合必须改为当前 active leaf 集合
- 解决方案：在 `RefineWithSplitQueue` 内新增 `enqueueActiveLeaves` 递归入口，从 `_rootA`、`_rootB` 向下遍历当前 active topology。遇到 active leaf 时再调用原有 `enqueueCandidate`，internal node 继续递归到 left / right child。这样保留 priority queue 的预算、score 排序和 forced split 后 child 重新入队逻辑，同时修正候选入口，使每一次相机触发 rebuild 时所有 active leaf 都能重新计算 `ComputeScreenErrorScore` 并决定是否继续 split。为避免后续再次误判，还新增 `ClassicRoamStats::ActiveSplitCount`，将状态统计和本帧事件统计分开；UI 显示“活跃 Split”和“本帧 Split”。另外新增正式的无窗口诊断入口 `src/benchmark/RoamProbe.cpp`，`main.cpp` 只做 `--roam-probe` 参数分发，避免把探针逻辑堆在入口文件里
- 验证：用户确认视觉问题已修复。命令行探针 `./build/debug-fetch/bin/ParallelROAM --roam-probe` 使用同一个 `ClassicRoamMeshBuilder` 连续跑多个相机位置，验证持久拓扑会继续变化：`far triangles=823 activeSplits=821 frameSplits=821`，`center triangles=6985 activeSplits=6983 frameSplits=6162`，`near-corner triangles=8188 activeSplits=8186 frameSplits=2237 frameMerges=1034`，`center-return triangles=9408 activeSplits=9406 frameSplits=1618 frameMerges=398`。这些数据证明不同相机位置下 active triangle、active split、本帧 split 和 merge 都会变化，不再停留在第一次 build 的拓扑。`./scripts/run_smoke_test_fetch.sh` 通过，`cmake --build --preset debug-fetch` 通过，注释覆盖率检查为 `15.04%`，注释末尾中文句号/逗号检查无命中
- 后续：需要把 `--roam-probe` 继续扩展成可复现 benchmark scenario，最好支持指定高度图、相机路径、阈值、最大深度和输出 CSV；后续每次修改 Classic ROAM 的 split / merge / hysteresis 逻辑时，应先跑该探针确认 active leaf 入口没有回归。还需要在阶段 2 完成固定测试入口，把“root-only 候选队列”这类拓扑入口错误变成自动化回归测试，而不是靠人工观察 wireframe

### BUG-011：DOD 候选标记 pass 拆文件后未进入 CMake target

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3，DOD ROAM 并行候选标记和内部架构拆分期间
- 现象：将 split / merge candidate marking 从 topology pass 中拆到新的 `DataOrientedRoamCandidateMarking.cpp` 后，`debug-fetch` 构建暴露出新源文件没有纳入 `parallel_roam` target 的问题，导致新 pass 的实现不能被链接进应用
- 定位：实现层已经在 `DataOrientedRoamState.h` 暴露 `CollectSplitCandidates`、`CollectMergeCandidates` 和 `CanMergeNode`，`DataOrientedRoamTopology.cpp` 也改为调用这些函数，但 `CMakeLists.txt` 的 app source 列表缺少 `src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp`。头文件声明和调用点都存在时，构建系统遗漏源文件会让链接阶段找不到实现，属于拆模块时的构建集成错误
- Debug 过程：先确认新 pass 文件存在且函数签名与 header 一致，再检查 app target 的源文件列表。定位到 CMake source list 仍只包含原有 DOD 文件，没有包含新拆出的 candidate marking 文件。该问题不是算法逻辑错误，也不是 namespace 不一致，而是 build target 没有消费新增 cpp
- 解决方案：在 `CMakeLists.txt` 的 `parallel_roam` source 列表中加入 `src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp`，保持拆分后的 pass 文件和 app target 同步
- 验证：`cmake --build --preset debug-fetch` 通过；随后 `./build/debug-fetch/bin/ParallelROAM --benchmark --algorithm all --profile smoke --csv /private/tmp/roam_3d_smoke_after_split.csv` 通过，Classic 与 DOD 的 smoke 场景三角形数保持一致，GPU 不可用时按 benchmark 逻辑跳过；`./scripts/run_smoke_test_fetch.sh` 通过
- 后续：每次新增 `.cpp` 文件时，构建验证应优先跑 `cmake --build --preset debug-fetch`，并在拆模块提交前检查 `CMakeLists.txt` 的 source list 是否同步

### BUG-012：DOD 保守并发拓扑提交在 smoke benchmark 中出现额外性能回退

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3，DOD ROAM 保守并发 topology commit 接入期间
- 现象：保守并发提交初版通过了 DOD smoke benchmark，三角形数和拓扑验证结果正确，但单独运行 `--benchmark --algorithm dod --profile smoke` 后，`near-corner` 和 `center-return` 的 Debug 构建耗时明显高于预期。功能正确但性能与“并发提交应减少 topology 压力”的预期不一致
- 定位：`BuildInteriorMergeChunks` 在分桶阶段对每个 merge candidate 调用完整 `SafeInteriorMergeChunkId`，而该函数内部又调用 `CanMergeNode`。`CanMergeNode` 会计算当前节点和 base neighbor 的 screen error；候选标记阶段已经计算过一次 merge score，分桶阶段再次对大量候选重复做评分，导致 Debug smoke 中 topology 时间被重复校验成本放大。问题不是线程数量不足，也不是 benchmark 三角形数错乱，而是 merge 分桶把“结构安全检查”和“提交前完整校验”混在了一起
- Debug 过程：先排除并发运行多个验证命令导致的 CPU 争用，因为单独跑 DOD smoke 仍能看到耗时偏高。随后对比优化前后 CSV 中 `cpuTopologyMs`、`cpuCollectMs` 和 `cpuUtilizationPercent`，确认额外成本集中在 topology 路径而不是 collect 或 error evaluation。检查 merge 分桶代码后发现候选筛选阶段重复执行 `CanMergeNode`，这与候选标记 pass 的职责重叠
- 解决方案：把 merge interior 判定拆成两层：`HasMergeReadyChildren` 和 `HasMergeReadyDiamond` 只做便宜的拓扑形状检查，用于候选分桶；worker 真正提交前再调用带完整 score 校验的 `SafeInteriorMergeChunkId(..., true)` 和原有 `MergeNodeOrDiamond`。这样分桶阶段不再重复计算大量 screen error，同时保留提交前的安全校验和串行回退
- 验证：优化后单独运行 `./build/debug-fetch/bin/ParallelROAM --benchmark --algorithm dod --profile smoke --csv /private/tmp/roam_3e_optimized_dod_smoke.csv` 通过，输出仍为 `far=823`、`center=6985`、`near-corner=8188`、`center-return=9408` 三角形，拓扑错误为 0。对应 CSV 中 DOD `cpuWorkerCount` 在中心和近景为 8，`cpuTopologyMs` 为 `far 7.18025`、`center 45.7321`、`near-corner 22.1229`、`center-return 12.3545`。`all smoke benchmark` 和 `./scripts/run_smoke_test_fetch.sh` 也通过
- 后续：当前保守并发策略仍会为 chunk 安全性付出额外判定成本，后续如果要继续优化，应把 chunk id 缓存到 node pool 或候选快照中，并把 interior / boundary 候选数量输出到 benchmark CSV，避免只能从 CPU 时间间接判断覆盖面

### BUG-013：性能分析缺少统一运行时标准流程

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 3，Classic ROAM、DOD ROAM 和后续 GPU ROAM 横向比较准备期间
- 现象：性能分析主要依赖手动移动相机、观察 UI 即时数值或运行无窗口 `--benchmark`。这些方式能发现单点问题，但很难保证 Classic、DOD 和未来 GPU 版本使用同一条相机路径、同一组 UI 参数、同一段采样时长和同一套汇总口径。结果是平均耗时、最大帧耗时、三角形规模、CPU worker 数和 CPU 占用率容易因为观察方式不同而不可复现，也不方便把测试结果交给后续回归和阶段评审
- 定位：已有 `src/benchmark/TerrainLodBenchmark.cpp` 解决的是无窗口算法层 benchmark，适合 smoke 和标准路径回归，但它不覆盖真实应用中的相机控制、renderer 更新缓存、OpenGL mesh upload、UI 状态锁定和用户可见输出流程。运行时 UI 只显示当前帧统计，没有“启动一轮标准实验”的入口，也没有把整个过程写成表格输出。`TerrainRenderer::UpdateForCamera` 还会按相机位移阈值复用 mesh，如果直接用普通交互路径采样，会把“缓存复用帧”和“真实重建帧”混在一起，导致性能数据不适合作为算法横向比较
- Debug 过程：先回顾此前性能问题的定位方式：BUG-009 依赖临时 benchmark 判断 Debug 构建下 Classic 全局 repair 过慢，BUG-012 依赖 CLI smoke CSV 判断 DOD topology pass 的额外回退。这些手段能解决局部问题，但都不是用户在应用内一键复现的标准流程。随后检查现有 UI 和 Application 主循环，确认 `ImGuiLayer` 只有参数面板和即时统计，`Application::Run` 只支持自由飞行相机，`TerrainRenderer` 没有外部强制重建和重置算法持久拓扑的入口。由此确认问题不是单个算法统计字段缺失，而是缺少贯穿 UI、相机、renderer 和报告输出的运行时 benchmark 流程
- 解决方案：新增运行时 benchmark 流程：UI 面板加入“开始 Benchmark”按钮；Application 保存用户当前面板状态和相机姿态后，依次运行 `Classic CPU ROAM` 与 `Data-Oriented CPU ROAM`；每个算法都从地形 `Z+` 边中点上方出发，朝向地形中心，在 10 秒内用 smoothstep 平滑移动到中心上方；测试期间锁定 UI 参数，并在画面中心上方显示 `正在应用xxx算法进行性能测试`。为保证每个采样点都代表真实算法输出，`TerrainRenderer` 增加 `RequestMeshRebuild` 和 `ResetTerrainLodAlgorithm`，benchmark 帧会绕过普通相机位移缓存并在切换算法时清空持久拓扑。新增 `RuntimeBenchmark` 模块把逐帧样本写入 `benchmark-output/runtime-benchmark-*.csv`，并输出 Markdown 汇总表，统计样本数、平均/最大帧耗时、平均/最大 ROAM 耗时、平均/最大三角形数、平均/最大节点数、平均/最大 CPU 占用、最大 worker 数、最大深度和最大拓扑问题数
- 验证：`cmake --build --preset debug-fetch` 通过；`./scripts/run_smoke_test_fetch.sh` 通过，窗口 smoke 能正常创建 OpenGL context 并退出；`git diff --check` 通过；源码注释检查确认无中文句号和阶段标签；注释覆盖率为 `15.01%`，连续注释最大 3 行。运行时完整 20 秒流程需要在交互窗口中点击“开始 Benchmark”人工触发，输出目录为 `benchmark-output/`
- 后续：Classic、DOD、GPU 三算法序列和 `--runtime-benchmark` 自动入口已在 BUG-016 完成；正式性能报告仍应补充构建类型和 VSync 状态，避免 FPS 或 frame ms 被运行环境误读

### BUG-014：运行时 benchmark 报告缺少关键配置并混淆最大深度口径

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3，运行时 benchmark 输出和极限地形参数对比期间
- 现象：用户使用更高精度高度图和更高最大深度跑完 benchmark 后，报告只显示平均/最大三角形数、节点数和单个 `Max Depth` 字段，没有记录本轮使用的高度图、地形尺寸、高度缩放、最大深度设置和距离权重。用户在 UI 中设置最大深度为 20，但旧报告显示 `Max Depth=17`，容易误判为 benchmark 没有使用 UI 参数或三角形数量记录有误
- 定位：运行时 benchmark 的采样数据来自 `TerrainRenderer::Stats()`，旧版 `TerrainRenderStats` 只暴露高度图尺寸、三角形数和算法实际达到的 `MaxActiveDepth`，没有把 UI 配置中的高度图路径、地形尺寸、高度缩放、最大深度设置和距离权重一起带入报告。`RuntimeBenchmarkSummary::MaxDepthReached` 又在 Markdown 中显示为 `Max Depth`，把“配置上限”和“当前相机路径实际展开到的最深层级”混成一个表头
- Debug 过程：先检查最新输出 `benchmark-output/runtime-benchmark-20260706-150156.md` 和对应 CSV，确认旧报告最大三角形来自逐帧 `stats.TriangleCount`，即 OpenGL 实际提交 index 数除以 3；再检查 `Application::RecordRuntimeBenchmarkSample`、`TerrainRenderer::Stats()` 和 `RuntimeBenchmark.cpp`，确认采样时并没有丢帧后重算三角形，而是报告缺少配置上下文。三角形数与用户手动观察 UI 不一致的主要解释是运行时 benchmark 只统计固定 10 秒路径上的样本，手动飞行到其他位置或 benchmark 完成后恢复原相机时，UI 可能显示更高的当前帧三角形数
- 解决方案：扩展 `TerrainRenderStats`，加入 `HeightMapPath`、`RoamMaxDepthSetting`、`RoamSplitThreshold`、`RoamMergeThreshold` 和 `RoamDistanceScale`，并继续记录 `TerrainSize`、`HeightScale` 和实际达到深度。运行时 benchmark CSV 在时间序列前写入高度图路径、宽高、地形尺寸、高度缩放、最大深度设置、阈值和距离权重；Markdown 顶部新增本轮配置块，汇总表把 `Max Depth` 拆成 `Config Max Depth` 和 `Reached Max Depth`。左上角详细性能面板也补充“设置深度”，和“实际深度”并排展示
- 验证：`cmake --build --preset debug-fetch` 通过；`./scripts/run_smoke_test_fetch.sh` 通过；`git diff --check` 通过。新报告生成后应能在 Markdown 顶部看到高度图路径、地形尺寸、高度缩放、最大深度设置、距离权重和阈值，并在汇总表中同时看到设置深度与实际达到深度
- 后续：自动 `--runtime-benchmark` 和 GPU benchmark 已在 BUG-016 复用这套配置字段；正式性能报告仍建议额外写入构建类型和 VSync 状态

### BUG-015：GPU ROAM-like 偶发生成跨场景长三角形

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 4 GPU split-only topology、active leaf compaction 和 indirect draw 联调
- 现象：GPU ROAM-like 在相机移动或多次重建后偶发出现从地形表面伸向远处的巨大细长三角形；普通 LOD 接缝问题只会在相邻层级边界形成裂缝或 T-junction，因此该现象不是单纯接缝错误
- 定位：node SSBO 只上传有效节点前缀，预留容量尾部由 `glBufferData` 创建且内容未定义；split 后的 compaction 却扫描整个 `nodeCapacity`，随机尾部标志可能命中 `ActiveLeaf`。此外 split pass 原先先增加 `allocatedNodeCount` 再锁定父节点，并发提交失败时会留下已计数但未写入的 node slot。mesh emit 随后把这些未定义 domain UV 转成世界坐标，最终由 indirect draw 画成长三角形
- 解决方案：active leaf compaction 改为以 GPU `allocatedNodeCount` 为有效上界，并排除已 split 节点；split-only pass 改为先原子锁定父节点再分配 child，失败时恢复父节点 flag；mesh emit 将 draw count 钳制到输出容量，并对 node index、leaf flag 和 `[0,1]` domain UV 做防御校验，异常 leaf 输出退化三角形而不是越界几何
- 验证：`relwithdebinfo-fetch` 构建通过；新增 `--gpu-smoke-test`，在 NVIDIA RTX 5090 / OpenGL 4.3 上连续强制执行 32 帧 GPU topology、compaction、emit 和 indirect draw，compute shader 编译与运行通过并返回退出码 0；普通 `--smoke-test` 继续用于 CPU 路径回归
- 后续：GPU split-only 仍是实验层，尚未把 GPU 拓扑写回 CPU DOD state；后续应增加 GPU active leaf 全量 readback validator，检查 domain、parent/child 和 diamond 约束，而不是只依赖可视化发现异常

### BUG-016：运行时 benchmark 只有 GPU 字段但不执行 GPU 算法

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 4 GPU compute、mesh emit 和 indirect draw 已接入 renderer 后
- 现象：runtime CSV 和 Markdown 已包含 GPU compute、上传和回读字段，算法名称也支持 `GPU ROAM-like`，但 UI“开始 Benchmark”的算法序列仍只有 Classic 和 Data-Oriented；无窗口 CLI 没有 OpenGL context，只能把 GPU 标为 skip，因此项目无法生成真实 GPU 横向性能数据
- 定位：缺口位于 `Application::StartRuntimeBenchmark` 的固定算法序列，而不是报告聚合层；报告已经能处理 GPU 统计，但没有带 active OpenGL context 的状态机把 GPU 算法送入同一相机路径
- 解决方案：runtime benchmark 在 OpenGL 初始化后查询 GPU capability，支持时按 Classic、Data-Oriented、GPU 顺序执行，不支持时把 skip 原因写入报告；新增 `--runtime-benchmark` 自动入口，完成后写出 Markdown / CSV 并退出；算法重建失败时中止采样并返回非零，避免把旧 mesh 统计记为 GPU 成绩
- 验证：在 NVIDIA GeForce RTX 5090 D / OpenGL 4.3 上完整运行约 30 秒，报告包含三行算法结果；GPU 路径采集 2453 个样本，`gpuComputeMilliseconds` 平均约 0.0952 ms、最大约 1.848 ms，上传/回读字节非零，进程退出码为 0
- 后续：当前 GPU ROAM-like 仍包含 CPU DOD topology baseline，报告已注明 `GPU ms` 只代表 compute passes；后续 GPU topology 完全持久化后，应把 CPU baseline 时间和纯 GPU topology 时间进一步拆列

### BUG-017：GPU ROAM-like 被每帧资源上传、buffer 重分配和同步读回拖慢

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 4 GPU ROAM-like runtime benchmark 性能分析期间
- 现象：同一条 runtime benchmark 路径下，GPU ROAM-like 的 GPU compute 时间很低，但整体 LOD 和 frame time 仍慢于 Data-Oriented CPU ROAM。旧报告只能看到 `gpuComputeMilliseconds`、CPU-GPU upload bytes 和 readback bytes，无法判断慢在 snapshot 打包、buffer 分配、dispatch CPU wall time、timer query 等待还是 counter readback 等待。用户观察到 GPU 版“算得快但整体慢”，需要先去掉明显的 CPU-GPU 交界开销，再补齐分项计时。
- 定位：`GpuRoamMeshBuilder` 每帧都会把 DOD 拓扑快照上传到 node SSBO，并且用 `glBufferData` 重新分配 active leaf、screen error、candidate、vertex、index 和 indirect draw buffer；height map texture 也在每次 build 中重新组装 float 数组并调用 `glTexImage2D`。此外旧路径在 `glEndQuery` 后立刻调用 `glGetQueryObjectui64v(..., GL_QUERY_RESULT, ...)`，随后用 `glGetBufferSubData` 读回 counter 和 debug sample，这会把 CPU 卡在 GPU 完成本帧 compute 之后。`TerrainRenderer::UploadMesh` 的 CPU mesh VBO/IBO 也使用每次 `glBufferData` 的方式，导致 DOD / Classic upload 统计混有额外重分配成本。
- Debug 过程：先对最新 runtime CSV 做分项汇总，发现 GPU compute 平均只有约 `0.19ms`，但 GPU 路径 `lodTotalMilliseconds - cpuUpdateMilliseconds - cpuUploadMilliseconds - gpuComputeMilliseconds` 的未归因时间明显高于 DOD。随后检查 `GpuRoamTerrainLodAlgorithm::BuildRenderData` 和 `GpuRoamMeshBuilder::RunGpuComputePipeline`，确认当前 GPU 版仍先跑 CPU DOD topology，再上传 GPU snapshot；代码里还存在每帧 `glTexImage2D`、多处 `glBufferData`、立即 timer query result 和立即 `glGetBufferSubData`。这说明瓶颈不是 shader 计算，而是资源生命周期和同步策略。
- 解决方案：GPU height map texture 改为按 `HeightMap::SourcePath()`、宽和高缓存，只在首次使用或切换高度图后上传；GPU SSBO、GPU vertex / index buffer 和 indirect draw buffer 增加容量缓存，只有容量不足时才 `glBufferData`，容量足够时只更新必要前缀或直接复用；CPU mesh VBO / IBO 同样改为容量复用和 `glBufferSubData` 更新；GPU timer query 和 counter readback 改为 4 slot ring buffer，当前帧提交后只标记 pending，后续 slot 轮转时再读取旧 query 和 counter，避免每帧在 `GL_QUERY_RESULT` 上强制同步；取消每帧 active leaf / screen error debug sample 立即读回，保留延迟 counter 作为拓扑计数防御校验；`BuildGpuRoamBufferSnapshot` 中 active leaf 标记从 `std::unordered_set` 改为按 node index 直接寻址的 `std::vector<std::uint8_t>`，避免每帧为几万 leaf 做哈希插入和查询；新增 `GpuSnapshotBuildMilliseconds`、`GpuBufferAllocationMilliseconds`、`GpuDispatchWallMilliseconds`、`GpuQueryWaitMilliseconds` 和 `GpuReadbackWaitMilliseconds`，并接入 `TerrainLodStats`、`TerrainRenderStats`、ImGui 详细性能面板、runtime CSV 和 Markdown 汇总表。
- 验证：`cmake --build --preset relwithdebinfo-fetch --parallel` 通过；`./build/relwithdebinfo-fetch/bin/ParallelROAM.exe --benchmark --algorithm all --profile smoke` 通过，Classic 和 DOD 仍保持 smoke 场景拓扑正确，GPU 在无窗口上下文中按预期 skip；`./build/relwithdebinfo-fetch/bin/ParallelROAM.exe --gpu-smoke-test` 在 NVIDIA GeForce RTX 5090 D / OpenGL 4.3 上通过，能创建窗口、编译/运行 GPU compute、执行 GPU topology / compaction / mesh emit / indirect draw 并正常退出。追加 active leaf 标记数组优化后，`cmake --build --preset relwithdebinfo-fetch --parallel` 再次通过。完整 runtime benchmark 需要用户明确触发后再生成新报告，本条不把未完成的 runtime 数据写成验证结论。
- 后续：GPU 路径仍保留 CPU DOD topology baseline 和每帧 node snapshot 上传，若要进一步超过 DOD，需要让 GPU 持久持有拓扑状态或引入 dirty range / persistent mapped buffer；延迟 readback 后，报告中的 GPU compute 时间代表最近完成 slot 的结果，适合性能趋势观察，但如果要逐帧严格对齐 GPU 计时和当前帧 topology 结果，需要在 CSV 中加入 readback slot frame id。

### BUG-018：Windows 上 CPU 利用率统计被 `std::clock()` 低报

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 4 runtime benchmark 跨平台性能分析期间
- 现象：同一套 DOD 多线程路径在 macOS 测试时 CPU 利用率最高可到约 `320%`，但 Windows RTX 5090 D 环境下 Classic、DOD 和 GPU 路径的 `CpuUtilizationPercent` 长时间都接近 `100%`。这与 DOD 代码中 `ErrorEvaluationWorkerCount`、`CollectWorkerCount`、`CandidateMarkWorkerCount`、`EmitWorkerCount` 和 `TopologyCommitWorkerCount` 都能达到 8 的事实不一致，容易误判为 Windows 上 DOD 没有真正并行。
- 定位：`src/algorithms/TerrainLodProfiling.h` 使用 `std::clock()` 捕获 build 前后的 CPU 时间，并用 CPU time / wall time 计算单核 100% 口径的利用率。macOS / POSIX 环境下该值表现得更接近进程累计 CPU 时间，因此多线程可以超过 100%；但 Windows / MSVC 下 `std::clock()` 不能可靠表示进程所有线程累计 CPU 时间，结果经常接近 wall time，导致多线程 CPU 利用率被压在约 100%。问题不在 DOD thread pool 或 worker 分发，而在 profiling 采样 API 的跨平台语义不一致。
- Debug 过程：先检查最新 runtime CSV，发现 DOD `cpuWorkers=8`、`AvgCpuUpdateMs=5.061ms`，明显快于 Classic 的 `12.095ms`，说明 DOD 并行和 SoA 优化确实生效；但 `AvgCpuPercent=100.72%` 与 macOS 上的 320% 现象冲突。随后检查 `CaptureTerrainLodCpuSample()` 和 `ComputeCpuUtilizationPercent()`，确认采样源是 `std::clock()`。结合 Windows/MSVC 对 `std::clock()` 的实现差异，确认这是统计口径 bug，而不是算法只能利用一个核心。
- 解决方案：将 `TerrainLodCpuSample` 中的 `std::clock_t ProcessCpuTime` 改为毫秒单位的 `ProcessCpuMilliseconds`。Windows 路径使用 `GetProcessTimes(GetCurrentProcess(), ...)` 读取 process kernel + user time，并按 FILETIME 的 100ns tick 转成毫秒；macOS / Linux 路径使用 `getrusage(RUSAGE_SELF)` 读取 `ru_utime + ru_stime`。`ComputeCpuUtilizationPercent()` 保持原有公式不变，仍按“一个逻辑核心满载等于 100%，多线程可超过 100%”输出。
- 验证：`cmake --build --preset relwithdebinfo-fetch --parallel` 通过。未自动运行新的 runtime benchmark；修复效果需要用户下次手动运行 benchmark 后，从 CSV / Markdown 中观察 DOD 的 `Avg CPU %` 是否能在 Windows 上超过 100%。
- 后续：如果后续需要更细的 CPU profiling，可进一步拆分每个 pass 的 CPU 利用率，或记录 thread pool active task 时间；当前修复只保证跨平台总进程 CPU 时间口径一致。

### BUG-019：Classic ROAM 的误差、预算、可见性和远距回收语义不完整

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 2 Classic CPU ROAM 基线复核
- 现象：旧实现只在节点创建时采样三边中点和重心，没有把深层最大误差传播给父节点；名为 `ComputeScreenErrorScore` 的分数不读取 FOV、投影或 drawable 尺寸；没有活动三角形硬预算和视锥感知；merge 候选只按 pass 开始时的快照处理，深层回收后新满足条件的 parent 要等下一个 Build。
- 定位：误差缓存位于 node 局部值，Classic adapter 只转发相机位置；split queue 没有预算 token；merge 使用固定候选集合；统一视图中的 frustum planes 未进入 Classic scoring。
- 解决方案：`dde13e5` 为两个根预计算完整方差树并传播子树最大误差；`fa83153` 使用 View、Projection 和 drawable height 计算像素 SSE；`77a9b6f` 加入默认 20,000 活动 leaf 硬预算和 forced-chain token 预留；`54e8915` 把 merge 改为成功后动态入队 parent 的最小堆；`57ce3cb` 使用方差扩张 AABB 对六个 inward plane 做视锥测试。
- 验证：OpenGL 与 D3D12 RelWithDebInfo 构建通过；CTest 4/4 通过；Classic 六视点 smoke PASS，所有 topology issue 为 0。`center -> away` 在相机位置不变时从 528 个活动三角形回收到 2，验证方向/视锥生效；所有帧均未超过 20,000 预算；`far-return` 单次 Build 可从深层拓扑回收到 376 个活动三角形。DOD smoke 同样 PASS，确认统一 benchmark 视图扩展没有破坏对照算法。
- 后续：为方差父子单调性、预算 closure、动态 merge queue 和 frustum AABB 增加 Classic 专用单元/属性测试；用 profiler 区分首次方差预计算、稳态 SSE/frustum、queue、emit 和上传成本。

### BUG-020：DOD 与 Classic 的 LOD 质量和预算口径发生分叉

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 3 Data-Oriented CPU ROAM 对照基线复核
- 现象：Classic 升级完整方差、像素 SSE、视锥感知、硬预算和同帧级联合并后，DOD 仍只缓存节点局部误差、只接收相机位置、使用 unitless 距离启发式、无活动 leaf 上限，merge 也依赖 pass 开始时的固定候选。两者虽然共享接口，但已无法在同一质量与资源约束下比较数据布局和并行化收益。
- 定位：`DataOrientedRoamPipeline` 的 view 输入退化为 `CameraPosition`；SoA 节点没有方差树归属/索引；`ComputeScreenErrorScore` 不读取 Projection、drawable height 或 frustum planes；并行 topology commit 没有跨 worker 的统一预算；`CommitInteriorMergeChunks` 不返回新满足条件的父层候选。
- 解决方案：为两个根预计算完整方差树，并在 SoA 中加入 `VarianceTreeIndex/VarianceIndex`；builder 改为接收完整 `TerrainLodViewInput`，使用与 Classic 相同的像素 SSE 和方差扩张 AABB 六平面测试；加入默认 20,000 活动 leaf 预算，所有串行、并行和 forced split 通过同一原子 token 消费上限；保留安全 chunk merge 并行预提交，同时把成功回收后新可合并的 parent 放入动态最小堆，单次 Build 内持续向上级联；UI、renderer 重建判定和 benchmark 断言同步覆盖 Classic/DOD。
- 验证：OpenGL 与 D3D12 RelWithDebInfo 构建通过；CTest 4/4 通过；Classic/DOD 六视点 smoke 均通过并得到相同活动三角形序列 `7072/528/2/2110/376/528`，所有 topology issue 为 0 且未超过 20,000 预算；`center -> away` 单次 Build 从 528 回收到 2。DOD standard 64 帧也通过，活动三角形范围为 `12906..20000`，实际触达硬上限但没有越界。
- 后续：评分、视锥和最终硬预算口径已由 BUG-021 迁移完成；GPU topology 状态持久化、GPU merge 提交和依赖感知的全局预算调度仍是后续独立工作。

### BUG-021：GPU ROAM-like 与 CPU ROAM 的评分和预算口径分叉

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 4 GPU ROAM-like 横向比较复核
- 现象：Classic/DOD 已使用完整方差、像素 SSE、六平面视锥和 20,000 leaf 硬预算，但 OpenGL/D3D12 原生 GPU pass 仍按局部高度、距离和边长的 unitless 分数决定 split，忽略 FOV、drawable height 和相机方向；DOD 快照后的额外 GPU split 也可能越过统一预算。UI 与 runtime CSV 同时保留两套阈值，三算法相同参数不代表相同质量或资源上限。
- 定位：GPU node record 已携带 DOD 的 `GeometricError`，但 error/candidate shader 重新采样旧式局部误差；GPU 常量缺少 View、projection scale、drawable height 和 frustum planes；node/leaf buffer 按全部输入 leaf 可再 split 一层扩容，counter 没有剩余预算 token；`TerrainLodSettings`、renderer、UI 和 runtime benchmark 仍暴露旧字段。
- Debug 过程：先沿 DOD snapshot、OpenGL GLSL、D3D12 HLSL、counter readback、renderer rebuild 和 benchmark 参数链逐项对照 Classic/DOD 公式；再确认 GPU merge candidate 只是 shadow 数据，真正跨帧 merge 仍由 CPU DOD 动态 queue 完成，因此本次只统一可真实保证的评分、可见性和最终 leaf 上限，不把混合管线误写成完整 GPU 拓扑实现。
- 解决方案：两个 GPU 后端都直接读取快照中由完整方差树传播的 `GeometricError`，使用显式 CPU 兼容双线性采样、View 中心深度、`Projection[1][1]`、drawable height 和正交投影分支计算像素 SSE，并以方差扩张世界 AABB 对六个 frustum plane 做保守测试；公共 UI/settings/CLI/CSV 移除 `DistanceScale` 与 unitless 阈值。CPU DOD 快照先占用 `TriangleBudget`，GPU split-only invocation 通过共享原子 counter 领取剩余 token：边界 split 消费 1、diamond pair 消费 2，claim/allocation 失败归还，buffer 也只按剩余预算扩容；延迟 readback 校验 active/node 计数和 token 守恒。
- 验证：OpenGL 与 D3D12 RelWithDebInfo 构建通过，D3D12 九个 shader target 重新编译；OpenGL test tree 的 CTest 4/4 通过。RTX 5090 D 上 OpenGL 4.3 与 D3D12 `--gpu-smoke-test` 均以退出码 0 通过，应用级断言检查 GPU packet 非空、最终三角形不超共享预算，以及 CPU DOD 持久拓扑三类 issue 为零。无窗口 `--benchmark --algorithm all --profile smoke` PASS，Classic/DOD 六帧均为 `7072/528/2/2110/376/528`，GPU 按无上下文规则 skip。
- 后续：GPU 新 child 的 `GeometricError` 当前为 0，且 GPU split 结果不回写下一帧 CPU DOD；GPU merge candidate 仍不提交，split token 又按并发 append 顺序而非全局 error priority 分配。后续应实现持久 GPU variance/topology、完整 forced closure 与 merge/recycle，并用 GPU 几何 readback 或离线渲染质量评估验证额外 split 后的裂缝和误差。

### BUG-022：Runtime 报告把算法阶段折叠为 CPU update 和 GPU compute

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：阶段 4 三算法 runtime benchmark 统计复核
- 现象：旧 Markdown 只有 `CPU Update`、原生 `Split/Merge/Emit/Validate` 和一个 `GPU compute` 数字。DOD 的误差评估/候选标记、Classic 的预算与最终 leaf 收集，以及 shader 内初始压缩、误差评估、候选标记、split 提交、计数重置、最终压缩和 mesh emit 都无法单独比较。报告即使显示 GPU 很快，也不能回答具体是哪一个算法阶段快或慢。
- 定位：`TerrainLodStats` 已有少量 CPU 推导桶，但 renderer/UI 没有完整转发；OpenGL 用一个 `GL_TIME_ELAPSED` 包围整条 dispatch 链，D3D12 只在首尾放两个 timestamp；`RuntimeBenchmark.cpp` 再把结果汇总为一个超宽表。原有 `CpuDecision/CpuTopology/CpuCollect` 还是相互重叠的推导值，不能相加解释 `CpuUpdate`。
- Debug 过程：沿 Classic/DOD builder、两个 GPU 后端、统一 stats、renderer、Application、ImGui 和 CSV/Markdown 全链追踪计时点。确认 Classic 的 screen error 在 split 队列扫描/弹出时就地计算，不能伪造独立 error pass；DOD 已有批量 error、active-leaf collect、split/merge candidate mark 子计时；OpenGL 的 leaf reset 原来是 CPU `glBufferSubData`，与 D3D12 reset compute shader 不同，不能直接标成相同 shader 阶段。
- 解决方案：CPU 统一为互斥阶段：prepare、merge candidate、merge topology、预算 leaf collect、error eval、split candidate、split topology、最终 leaf collect、mesh emit 和 finalize；Validate 没有独立互斥桶，仍与原生 Split/Merge/Emit/Validate 一样作为可能重叠的包络另表展示。OpenGL 新增一个 active-leaf reset compute shader，并用七个顺序、非嵌套 `GL_TIME_ELAPSED` query；D3D12 用八个 timestamp 边界得到相同七段。统一接口、renderer、UI、两种 CSV 和 Markdown 全部改为具体 pass 名称，`GpuPassSumMilliseconds` 只表示七段之和。实验图表脚本兼容旧 CSV，但旧聚合值明确标为“未拆分”，不反推历史子阶段。
- 验证：OpenGL/D3D12 RelWithDebInfo 构建通过；CTest 4/4 通过；RTX 5090 D 上两个后端 `--gpu-smoke-test` 均退出码 0。当时的 D3D12 运行时验证报告为 `benchmark-output/runtime-benchmark-20260731-141441.md`，OpenGL 报告为 `benchmark-output/runtime-benchmark-20260731-141428.md`；该七段物理 pass 口径随后被 BUG-023 的 ROAM 逻辑阶段口径取代。
- 后续：GPU query 仍是环形槽延迟结果，严格逐帧关联需在 CSV 增加提交序列号；短测试中单个 shader pass 低于 0.01 ms，Markdown GPU 表已提升到四位小数，正式性能结论仍应使用 10-60 秒采样并报告 P95/P99。

### BUG-023：GPU 计时按 dispatch 拆分，但没有对齐 ROAM 逻辑阶段

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：BUG-022 七段 shader 计时复核
- 现象：报告虽然不再只有一个 GPU 总时间，但仍把 `Initial leaf compaction`、`Candidate marking`、`Active-leaf reset` 等物理 dispatch 当作 ROAM 阶段。尤其一个 candidate shader 同时扫描 split leaf 与 merge parent，无法比较 split/merge 决策；报告也没有明确说明 GPU merge candidate 从未提交、持久 merge 和邻接修复仍由 CPU DOD baseline 完成。
- 定位：`GpuRoamCandidateMarking`/`CSCandidateMarking` 在一个 dispatch 中执行两个不同扫描域；`GpuRoamSplitOnlyTopology` 只提交边界 split 或直接 base-neighbor pair，不递归传播完整 forced-split chain；GPU 路径进入 shader 前已经运行一次完整 DOD `BuildGpuSnapshot`。BUG-022 记录的是设备命令边界，不是算法责任边界。
- 解决方案：OpenGL candidate shader 新增 split/merge 模式并分别 dispatch；D3D12 拆成 `CSSplitCandidateMarking` 与 `CSMergeCandidateMarking` 两个入口。统一 stats、renderer、UI、CSV 和图表脚本增加独立 split/merge candidate 字段。Runtime Markdown 新增 `ROAM logical stage comparison`，按 prepare、merge candidate、merge commit、split 前 leaf collection、leaf error/frustum、split candidate、split/direct-diamond commit、split 后 leaf collection、mesh emit、finalize 对齐 Classic、DOD、GPU-like CPU baseline 与 GPU shader；GPU 未实现的 merge commit 显示为 `N/A`。
- 验证：OpenGL/D3D12 RelWithDebInfo 构建、CTest 4/4 与两个 `--gpu-smoke-test` 均通过。最终 OpenGL 报告 `benchmark-output/runtime-benchmark-20260731-222129.md` 中 split/merge candidate 分别为 0.0048/0.0055 ms；D3D12 报告 `benchmark-output/runtime-benchmark-20260731-222147.md` 中分别为 0.0030/0.0030 ms。OpenGL 回读为 96 B（32 B counter + 8 个 query result），D3D12 为 104 B（32 B counter + 9 个 timestamp）。
- 后续：GPU merge topology、完整邻接发布和递归 forced-split chain 尚未实现，不能把当前 GPU-like shader 列视为完整 GPU ROAM。初始化时的 variance/geometric-error rebuild 仍不在稳定帧阶段表中，应在专门的 reset/initialization benchmark 中测量。

### BUG-024：硬预算满载时转向后的新入屏区域延迟细分

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：Classic/DOD 视锥感知和硬预算交互复核
- 现象：活动三角形达到 `TriangleBudget` 后，已经离开屏幕的区域再次进入视野时仍保持粗网格；继续转动到更大角度后才突然重新细分。截图中 Classic 恰好保持 `20000` 个活动三角形，说明问题发生在预算饱和状态。
- 定位：renderer 在原地转向时会逐帧触发 LOD Build，延迟不是相机重建门限或视锥平面滞后。Classic 与 DOD 的旧 merge pass 只允许 `score <= MergeThreshold` 的 parent 回收；预算没有剩余 token 时，新入屏的高分 split 即使排在最大堆顶部也只能被拒绝。只有旧视野继续转出、低分 parent 穿过固定 merge 阈值后，才会释放 token，因此出现角度相关延迟。GPU ROAM-like 的持久拓扑来自 DOD，同样继承该问题。
- Debug 过程：新增 `budget-reentry` 无窗口场景，以 `512` leaf 硬预算先聚焦左侧，再保持相机位置不变并按约 7-8 度步进转向。临时关闭预算交换分支时，Classic/DOD 在前三次转向均为 `split=0, merge=0`，直到最后更大角度才各执行 `6 merge + 6 split`，稳定复现用户现象。
- 解决方案：正常 merge 仍严格使用 `MergeThreshold`。阈值回收完成后，若剩余 token 少于当前高分 split 需求，则计算本帧最高 split 分数，并只从拓扑上可安全合并、损失至少低一个 split/merge 迟滞区间的 diamond 中按低分优先释放 token。单帧交换量限制为 `min(128, max(1, TriangleBudget/32))`，防止一次转向大面积推倒拓扑；释放后继续复用原高分 split 队列、forced split 预算预留和 diamond 邻接维护。Classic 与 DOD 使用相同策略，GPU-like 的 CPU DOD baseline 自动获得相同行为。
- 后续演进：上述有限批次方案会减少 DOD 实际执行的拓扑事务，使其与持续收敛的 Classic 无法进行同等工作量比较。当前 Classic 与 DOD 都已撤销该上限，持续执行 merge-first 资源交换，直到 `max(Q_s) <= min(Q_m)`；本条只记录历史修复过程。
- 验证：交换开启后，Classic/DOD 在 `budget-reentry` 的每个小角度步进中都在第一次 Build 完成 `15-17` 次等量 merge/split，活动三角形始终为 `512/512`，拓扑 issue 为 0；临时关闭交换时该场景按预期失败。OpenGL/D3D12 RelWithDebInfo 构建和完整 CTest 覆盖该 profile。
- 后续：当前实现是有界的串行优先级交换启发式，不等同于依赖感知的全局最优预算调度器；GPU 原生 split-only pass 仍按并发 append 顺序领取 CPU baseline 剩余 token。

### BUG-025：DOD 最终输出重复递归收集 Active Leaf

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：阶段 3 Data-Oriented CPU ROAM 输出和统计
- 现象：DOD 已用 `ActiveLeafNodes` / `ActiveLeafNodePositions` 增量维护活动叶，但每次 Build 在 split/merge 稳定后仍调用 `CollectLeafNodes`，从两个 root 递归遍历活动树并填充第二份 `FinalActiveLeaves`，随后 CPU emit、统计和 GPU snapshot 才读取该副本。
- 定位：冗余入口位于 `DataOrientedRoamPipeline::BuildInternal`；`BuildGpuRoamBufferSnapshot` 也绑定 `FinalActiveLeaves`。独立递归遍历对 validator 有价值，但正常输出路径没有在 Build 完成后到 emit/packing 之间修改拓扑，不需要用它重新发现 leaf。
- 解决方案：删除 `FinalActiveLeaves`，让 CPU emit、`AccumulateLeafStats` 和 GPU snapshot 直接只读消费 `ActiveLeafNodes`；DOD 的统一兼容字段 `CpuFinalLeafCollectMilliseconds` 置 0。`CollectLeafNodes` 仅保留给 `ValidateTopology`，继续交叉检查活动索引集合、重复项和反向 position。
- 验证：OpenGL/D3D12 RelWithDebInfo 构建通过，OpenGL CTest 6/6 通过；DOD standard 64 帧 PASS，活动三角形范围为 `12906..20000`。同参数 runtime 基线 `20260801-195558` 到最终报告 `20260801-221506` 中，DOD `Final leaf collect/view` 从 `0.2383 ms` 变为 `0.0000 ms`，平均 triangles/nodes 变化均小于 0.1%，最大 topology issues 保持 0。
- 后续：`CollectActiveSplitPaths` 仍从 root 递归生成跨帧 hysteresis path 集合，它维护的是不同状态，不能仅因本次 leaf 输出优化直接删除；是否值得改为增量维护需要单独分析正确性和成本。

### BUG-026：旧方差递推没有实现论文公式 (1) 的 Nested Wedgie Thickness

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：论文与 Classic/DOD 误差预计算对照
- 现象：旧实现虽然递归建立完整树，但每个节点取 `max(localError,leftError,rightError)`，其中 `localError` 来自三边中点与重心四点采样。它没有把 child plane 相对 parent plane 的 base-midpoint displacement 逐层累加，因此不能等同于论文公式 (1)；而且预计算只到运行时 `MaxDepth`，较小的运行时上限会遮蔽源 height map 更深层的误差。
- 定位：Classic 的 `ClassicRoamMeshBuilder::BuildVarianceSubtree` 与 DOD scoring 文件中的同名匿名递推各维护一份旧公式；GPU ROAM-like 从 DOD snapshot 读取 `GeometricError`，因此也继承旧语义。
- 解决方案：新增共享 `RoamNestedWedgie.h`。`ResolveNestedWedgieTreeDepth` 按 `max(MaxDepth, 2*ceil(log2(max(width-1,height-1))))` 选择预计算深度并限制到 20；`BuildNestedWedgieSubtree` 把最细层设为 0，其他节点执行 `max(leftThickness,rightThickness)+abs(baseMidpointDisplacement)`。Classic/DOD 删除各自旧递推，共用同一公式；GPU snapshot 自动继承新 thickness。
- 验证：新增 `roam_nested_wedgie` 属性测试，覆盖最细层为 0、有符号位移取绝对值、多层累计、129/513 source depth，并穷举小型几何 bintree 中每个 ancestor 对最细后代顶点的高度误差界；RelWithDebInfo CTest 7/7 通过。Classic/DOD smoke、budget-reentry 与 513x513 standard 64 帧均 PASS，逐帧输出规模保持一致且 topology issue 为 0；standard 三角形范围均为 `12906..20000`。RTX 5090 D 上 OpenGL 4.3 与 D3D12 `--gpu-smoke-test` 均以退出码 0 完成。
- 维护边界：真实小型 HeightMap 的 ancestor bound、非 `2^k+1` 输入和 source depth 上限仍可作为工程回归测试；连续双线性曲面解释与完整误差证明属于可选研究，不是当前完成条件。公式 (2)/(3) 已由 BUG-027 实现，但不能仅凭局部公式 (1)-(3) 宣称完整 guaranteed screen-space error。

### BUG-027：Center-Depth SSE 不是论文公式 (2)/(3) 的保守屏幕投影

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：Classic/DOD CPU error evaluation、OpenGL/D3D12 GPU error 与 merge candidate evaluation
- 现象：旧评分只用 triangle center 的 `view-space Z`、`Projection[1][1]` 和 drawable height 估计整片像素尺度。跨越较大深度范围的粗 triangle 可能低估近端；相机 pitch/roll 后，世界高度 thickness 在 camera depth 上的分量也未进入公式；wedgie 穿越 near plane 时只靠 `abs(depth)` 与 `0.05` clamp，不能继承论文局部 bound。
- 定位：Classic/DOD 的 `ComputeScreenErrorScore`、OpenGL `GpuRoamErrorEvaluation`/`GpuRoamCandidateMarking` 以及 `GpuRoamLike.hlsl::ScoreNode` 各自复制了 center-depth heuristic。GPU 常量只上传 `View`、`ProjectionScaleY` 与 `DrawableHeight`，没有完整 `ViewProjection` 和 drawable width。
- 解决方案：新增共享 `RoamScreenProjection.h`，把 triangle corners 与世界高度方向 `(0,worldThickness,0,0)` 变换到 homogeneous clip space，以 `clip.x/clip.y/clip.w` 和 `thicknessClip.x/thicknessClip.y/thicknessClip.w` 直接实现公式 (3)。三个角点分别求最小 denominator 与最大 pixel-scaled numerator；wedgie 触碰或穿越 inward near plane 时返回 `float max`。Classic/DOD 共用 helper；OpenGL/D3D12 shader 使用相同代数和常量布局。保留的平坦区域 `edge-density` 明确作为独立扩展，并改成三个端点的真实像素投影，不再复用 center depth。
- 验证：新增 `roam_screen_projection` 测试，对多组 FOV、aspect、camera pitch/roll、drawable 尺寸和 perspective/orthographic 投影在 triangle 内密集采样公式 (2)，所有样本均不超过公式 (3) bound；near-plane crossing 返回人工最大值，分辨率双倍时 bound 双倍。OpenGL/D3D12 RelWithDebInfo 构建通过，DXC 成功编译全部 GPU ROAM shader，OpenGL `--gpu-smoke-test` 退出码为 0。
- 维护边界：CPU/GLSL/HLSL score buffer 逐值一致性测试仍属于跨后端工程正确性验证；真实 nested parent/child 最终 priority 单调性测试只服务论文证明研究，不是当前完成条件。持久 dual queues 已在后续阶段实现，但最终 score 仍是 `max(geometricBound, edgeDensity)`，因此不能把局部保守 bound 直接表述为完整论文最优性保证。

### BUG-028：Classic 每次 Build 全量重建并上传 CPU Mesh

- 状态：`Fixed`
- 严重级别：`中`
- 发生阶段：论文 incremental triangle stripping 与当前 Classic 输出路径对照
- 现象：拓扑和双队列已经跨帧持久，但旧路径仍从 roots 递归收集全部 active leaves，重新采样每个 leaf 的顶点属性、构造新的 `TerrainMeshData`，adapter 再移动完整 vectors，renderer 最后上传整个 VBO/IBO。固定视点触发 Build 时，输出成本仍与活动三角形总数成正比。
- 定位：旧 `ClassicRoamMeshBuilder::Build` 在 topology 稳定后调用 `CollectLeafNodes` 与 `EmitLeafTriangles`；`TerrainLodRenderPacket` 只能拥有 CPU Mesh，没有借用生命周期、generation 或更新区间；OpenGL/D3D12 CPU 路径只接受全量上传。
- 解决方案：每个 active leaf 新增 `MeshSlot`，builder 持久维护 dense `_meshSlotOwners` 与 `_meshData`。split 复用 parent 槽并追加 right child；merge 写回 parent，删除另一 child，并以 move-last compaction 消除空洞。topology 提交只记录有序 edit，emit 阶段统一 replay、按 generation 去重 dirty slots 并合并 ranges。adapter 通过 borrowed Mesh 发布持久数据；OpenGL 只对 dirty ranges 调用 `glBufferSubData`，D3D12 为每个 frame slot 保存 pending full/range 状态，并在 slot 追赶多个 Build 时分别合并 vertex/index 区间。validator 交叉检查 root leaf 集合、slot owner、反向 `MeshSlot` 和局部 index 范围。
- 验证：新增 `incremental-emit` profile 和 CTest。129x129、528 个 active triangles 下，相同视点连续三次 Build 的计数依次为：首帧 full=1/updated=528；第二帧 full=0/updated=528，用于结束 Rebuilt 调试色；第三帧 full=0/updated=0/reused=528/dirtyRanges=0。Classic smoke 的大规模 split/merge/级联合并 6 帧全部通过且 topology issue 为 0，budget-reentry 5 帧维持 512 leaf 并全部通过。OpenGL 全部 CTest 9/9 通过，OpenGL 与 D3D12 RelWithDebInfo 均构建通过，D3D12 版 headless incremental profile 也通过。RTX 5090 D 上 0.5 秒 D3D12 runtime 采样得到 228 个 Classic 局部上传帧，没有任何一帧超过当帧完整 Mesh 字节数，稳定末段降为 0 B。
- 后续：当前实现对应论文“只修改受影响输出”的职责，但不是 triangle-strip 数据格式复刻。仍需增加独立 full-emit reference 与随机 split/merge 轨迹逐帧对照，并用有窗口 runtime benchmark 测量 changed slots、实际 upload bytes 与耗时的缩放关系。

### BUG-029：固定 10 秒路径导致算法获得不同数量的相机输入

- 状态：`Fixed`
- 严重级别：`高`
- 发生阶段：Classic、DOD、GPU ROAM-like runtime benchmark 公平性复核
- 现象：旧状态机用每种算法自己的 `RawDeltaSeconds` 推进 10 秒路径并逐帧采样，因此较快算法会在同一条几何路径上获得更多 Build 和更密的相机姿态。极限压力报告 `runtime-benchmark-20260810-003052` 中三种算法分别只有 37、61、32 个样本；默认路径报告 `runtime-benchmark-20260810-002908` 中则分别为 5615、3052、2435 个样本。平均耗时不仅混入算法成本，还混入不同的路径离散密度和拓扑事务序列。
- 定位：`Application::PrepareRuntimeBenchmarkFrame` 旧实现用 `ElapsedSeconds / DurationSeconds` 计算路径进度，`Application::CompleteRuntimeBenchmarkFrame` 也用墙钟时间决定算法切换；`RuntimeBenchmarkSample` 没有路径采样索引，报告只能记录 `timeSeconds`。
- 解决方案：runtime benchmark 改为有限的离散相机姿态序列。默认选项路径固定 600 点，极限压力路径固定 64 点；Classic、DOD、GPU 都从 `sampleIndex=0` 走到末点，每完成一个渲染样本才推进索引。`timeSeconds` 仅记录当前算法实际墙钟时间。CSV 新增 `pathSampleIndex`、`pathSampleCount`、`pathProgress`，Markdown 检查每种算法是否完整覆盖 `0..sampleCount-1`。CLI 新增 `--runtime-benchmark-samples`；旧 `--runtime-benchmark-duration` 只作为兼容入口，按每个名义秒 60 点换算。
- 验证：OpenGL RelWithDebInfo 构建通过；使用 `--runtime-benchmark-samples 4` 完成窗口实测，Classic、DOD、GPU 均输出 4 个样本，索引均为 `0,1,2,3`，进度均为 `0,0.333,0.667,1`；按索引分组后每组只有一个唯一相机坐标，Markdown 输出“采样完整性：完整”。
- 后续：采用旧 10 秒口径生成的报告仍可用于说明当时版本，但不能直接作为新口径下的逐点公平对比基线；正式优化实验应在固定采样点口径下重新运行多轮。

## 模板

```text
### BUG-XXX：标题

- 状态：
- 严重级别：
- 发生阶段：
- 现象：
- 定位：
- Debug 过程：
- 解决方案：
- 验证：
- 后续：
```
