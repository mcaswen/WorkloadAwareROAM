# CBT 同代 GPU 实验证据事实

日期：2026-09-16。范围：CBI-03；以 `platform_integration.md` 的资源发布契约为前提。

## 输入与调用边界

`ExperimentCase` 增加独立 `CbtCapacity/CbtArea/CbtValidation/CbtGeometry`。JSON 必须显式填写，Python schema 与 C++ 均检查四档容量、正面积和合法模式。CBT 仅支持 D3D12 平台实验，CPU-only/GL/profile 拒绝。CPU 配置与旧 task identity 不变，CBT参数另外进入 task 哈希。

CBT 的 `budget` 是 CPU 对照分组标签，不是容量或实际三角形上限；runtime 只使用 CBT 设置。捕获网格上限按动态容量加六基础槽检查。`PlatformMeshArtifact` 读取额度扩大到 `3*(1048576+6)` 顶点/索引，仍有限额。

## 文件、类型与所有权

| 文件/类型 | 当前职责与依赖 |
|---|---|
| `ExperimentCase.h/.cpp` | 独立值配置，不依赖算法对象或D3D12设备 |
| `ExperimentReplay.cpp` | 输入校验/设置映射、CPU或实际捕获网格校验、公共60列；CBT无mesh时N/hash留空 |
| `ExperimentPlatformRun.cpp` | 真实帧循环；Present后分派CPU输出或CBT捕获；证据成本在frame计时之外 |
| `d3d12/D3D12CbtMeshCapture.h/.cpp` | 借用三个GPU缓冲，持有一次readback与压紧诊断mesh；资源恢复后返回值 |
| `CbtExperimentRecords.h/.cpp` | 独立 `cbt.csv`，记录当前代、延迟采样代、参数、阶段、计数及捕获身份 |
| `runner.py` | 冻结原始输入/源码/程序与CBT来源清单SHA；不改算法 |
| `result_adapters.py` | 检查原始文件哈希、机会、故障和捕获代；缺失数据不补成0 |
| `quality.py` / 独立C++ probe | 复用原evaluator读取实际mesh；没有新增CBT专属质量公式 |

捕获器仅由 D3D12+CBT+EIP 构建链接。公共记录器不包含图形API；算法和渲染消费者不依赖捕获器。

## 捕获控制流

`CaptureCbtMesh` 要求帧已关闭，接收当前借用描述和地形尺度。首先验证ABI、实际buffer宽度及200MiB复制额度，然后等待已提交工作。一个READBACK buffer按偏移容纳 DRAW、全容量ActiveIndices和实际RenderVertices。

立即命令逐资源执行“原发布状态→COPY_SOURCE→复制→原状态”。顶点/活动索引恢复NON_PIXEL_SHADER_RESOURCE，DRAW恢复INDIRECT_ARGUMENT；因此不改变来源控制器的状态跟踪。函数不Reset、不Build、不追赶延迟统计。

映射后检查单实例/零起点/DRAW三倍布局，`ActiveBisectorCount` 必须等于实际N。逐活动槽取真实三个顶点；索引重复或越界直接失败。输出紧凑mesh只改变诊断排列，不生成替代几何。资源代、拓扑代与renderer当前stats在记录器中再次核对。

正常timing不调用捕获器，也不对CBT做全量hash；`frames.csv` 的当前N/hash为空。quality/visual只在冻结关键机会捕获，同机会的图片、矩阵、N、mesh/hash共同构成证据。

## 记录与异步语义

`frames.csv` 保留原60列。CBT的CPU时间是主机命令记录时间，CPU五阶段、CPU拆分/合并计数和线程数留空，不能当作GPU工作为0。帧包络可描述平台提交与等待，不能用CPU记录时间替代GPU完成时间。

`cbt.csv` 每机会记录资源/当前拓扑/classification/GPU计算/绘制代，年龄、丢弃、延迟N、容量、面积、模式、占用余量、故障、提交/传播/简化/模板计数与18阶段时间。无对应采样代的计时/计数保持空值。GPU计算和绘制的采样代可能不同，不能直接求和归给当前CPU帧。

有捕获时附实际N、资源/拓扑代、复制字节和同步/解码耗时。导出中出现FaultRecovery直接使回放失败。尾部未取得的异步样本保留缺失，不补隐藏更新。

`result_adapters.load_run` 对CBT不执行 `N<=budget`，而检查实际N与捕获记录相等、代次相等和容量边界；有hash/artifact却无N时拒绝。未捕获机会没有质量结论。

## 几何与质量验证

实际mesh依次检查有限坐标、正朝向、UV域、源双线性高度。容差 `2e-5*max(heightScale,1)` 仅用于CPU/GPU浮点源高度诊断，不参与独立Emax接受条件。此检查不是完整逐动态HeapID的CPU几何oracle；六基础槽另由来源blocking模式核对，完整动态域由独立evaluator核查。

CBI-03证据在忽略目录 `benchmark-output/cbt-2024/cbi-03/`。test129/Peking均24机会，面积20、动态128K、off/modified。Peking第15/23机会实际N=36241/37299，与同代mesh相符；test129第0/15为12/24982。

| 资产/机会 | sampled Emax px | Hmax | 参考样本数 |
|---|---:|---:|---:|
| test129/0 | 37.438866 | 1.266176 | 98817 |
| test129/15 | 2.862591 | 0.141176 | 98817 |
| Peking/15 | 1.091344 | 0.142195 | 1790881 |
| Peking/23 | 0.927970 | 0.244752 | 1790881 |

四项均missing/ambiguous/invalidGeometry/invalidProjection=0，evaluator hash与实际artifact一致。RMS仍是可见参数域样本等权，不称屏幕面积加权；最大值是sampled maximum，不是连续地形严格上界。

128K捕获每次复制20972520字节；test129/Peking证据帧平均捕获8.540/7.224ms，包含等待、分配、复制和解码，不计为算法更新时间。独立评价约0.4–0.84秒/帧。正常CBT24机会均无捕获、无故障，GPU计算样本按自己的代记录。

## 验证与未确认

Linux配置/测试复用已有Boost1.90，未引入新依赖；新增CPU case拒绝、6项Python证据检查通过；Windows两后端构建、真实GPU捕获和实际截图观察完成。截图确认地形与view一致，不替代覆盖检查。

UNCERTAIN：未证明GPU原子调度跨进程字节确定性，因此没有强行要求独立timing/visual运行hash一致。没有相同实际N的质量优劣结论；CBI-04负责有限比较。性能对照与具体噪声/回归分析见小规划结果，不用捕获关闭的代码路径事实替代实测。
