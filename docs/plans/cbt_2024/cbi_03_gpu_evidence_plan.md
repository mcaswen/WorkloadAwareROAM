# CBI-03 同代 GPU 网格与实验计量

日期：2026-09-16。上阶段提交：7157ea7。状态：实施前规划；沿已批准 Major Plan 自主闭环。

## 问题与目标

CBT 已真实绘制，但 EIP 只读取 CPU 网格，并将容量、预算和当前数量混在既有 CPU 字段中。直接把延迟计数写成当前 N、用 CPU 几何重建替代实际 GPU 输出，会破坏质量比较。

本阶段打通同一机会的 GPU draw state、活动索引、实际顶点、相机矩阵和离线质量输入；正常 timing 不读回完整网格。CBT 的参数与 GPU 统计独立记录，保留已有 CPU case/CSV 的身份与行为。

不修改来源分类、面积阈值、拓扑更新或几何规则；不做参数寻优、CPU/GPU 算法优劣结论，不扩通用 profiler。

## 已扫描的事实

- `ExperimentPlatformRun` 在 Present 后校验 CPU 网格；quality/visual 关键机会写 `TPIMSH01`。
- 来源帧结束时，顶点与 ActiveIndices 为 NON_PIXEL_SHADER_RESOURCE，draw state 为 INDIRECT_ARGUMENT。
- Active DRAW 是四个 uint，VertexCountPerInstance=3N、InstanceCount=1，起始项为零。活动列表映射物理槽位；基础六槽不属于动态容量。
- `D3D12GraphicsBackend.ExecuteImmediate` 已提供独立提交和等待，不需让捕获器进入算法控制器。
- 既有 `ReplayFrameWriter` 60 列与 `result_adapters` 严格对应；可保留公共列，CBT 专属字段单写有代次的 `cbt.csv`。
- `PlatformMeshArtifact` 当前读取上限为200k面，需要支持CBT明确有界容量，不把上限当成算法硬预算。
- 独立 evaluator 已读取实际 float 网格，参考是 raw U16 + bilinear；保留 sampled maximum、等权可见参数域 RMS 和逐点 Dmax。

## 文件职责与依赖

| 方式 | 文件 | 本次职责 |
|---|---|---|
| Create | `benchmark/experiment/d3d12/D3D12CbtMeshCapture.h/.cpp` | D3D12 专属读回/解码；持有临时 readback，不参与更新或排序 |
| Create | `benchmark/experiment/CbtExperimentRecords.h/.cpp` | 独立 CBT CSV，写当前/采样代、容量、实际捕获N、GPU阶段和捕获成本 |
| Extend | `ExperimentPlatformRun.cpp` | GPU/CPU 输出分派；只在质量/视觉证据帧捕获；明确同代身份 |
| Extend | `ExperimentReplay.h/.cpp` | 映射独立 CBT 设置；缺实际 mesh 时公共 N/hash 留空，不伪造零 |
| Extend | `ExperimentCase.h/.cpp`、case schema、catalog | 容量/面积/验证/几何模式；CBT仅D3D12；CPU原case兼容 |
| Extend | `runner.py`、`result_adapters.py` | 冻结CBT参数/来源身份，区分硬预算与槽池，检查同代捕获 |
| Extend | `PlatformMeshArtifact.h` | 诊断文件安全读取额度适配最高容量，仍限制分配 |
| Extend | CMake、EIP契约测试 | 只在D3D12+CBT+EIP链接捕获器；检查非法配置与空证据 |

依赖方向：EIP → renderer 借用描述 → D3D12捕获器 → 后端立即提交。算法不依赖 EIP；公共记录器不引入 D3D12。独立 evaluator 与捕获器不共享几何生成实现。

## 捕获契约

捕获入口只允许 Present 已关闭帧、下一次 Build 尚未发生时使用；参数是当前 `TerrainLodGpuOutput`、地形尺度和后端。返回网格与资源/拓扑代、读回字节/耗时。使用值描述保留当前身份，源资源仍由算法持有。

1. 验证非空句柄、精确跨度、资源描述容量、有限额度和当前帧已关闭。
2. 等待已提交工作，读取同一代 draw、ActiveIndices 和 RenderVertices。
3. 三个缓冲只在立即命令中转换为 COPY_SOURCE，复制后恢复原发布状态。
4. 验证 DRAW 的实例/起始/3N 布局；N 不超过动态容量加六基础槽及列表容量。
5. 逐活动序号读取物理槽位，拒绝重复/越界；取实际三顶点生成紧凑诊断网格，不调用 CPU CBT 生成器。
6. 检查有限坐标、非退化朝向、UV域和源高度关系；再交现有 evaluator 检查覆盖及质量。

第一版允许按容量读回顶点/活动缓冲，成本显式 O(capacity+N)，只用于离线诊断。不得把这笔成本漏记或放进正常更新；未来若优化捕获也不算算法加速。

捕获中不 Reset、不补跑更新、不修改候选、不根据延迟统计决定当前 N。任何布局或代次不符都保留失败，禁止用旧 CPU 网格补齐。

## 参数与记录契约

CBT case 显式给 `cbtCapacity`、`cbtArea`、`cbtValidation`、`cbtGeometry`。容量枚举为四档动态槽；常规为 off/modified。已有 `budget` 对CBT只保留跨算法工作负载分组的参考值，不能验收 N≤budget，也不进入CBT控制器。

`frames.csv` 的 CPU计时、帧包络与证据成本继续可用。CBT无当前mesh的机会，N/hash留空；实际捕获机会填真正 N/hash。CPU五阶段与事务统计对CBT留空。

`cbt.csv` 每机会记录：资源代、当前拓扑代、classification采样代、GPU计时采样代、绘制采样代、样本年龄/丢弃、延迟活动数/占用余量、故障恢复、面积/容量/模式、18阶段时间；有捕获时记录同代 N/代次/字节/耗时。

CPU update 是记录命令的主机时间，不能称GPU完成时间。GPU计算与绘制按各自采样代解释；不把跨代求和当一帧总成本。最后未取得时间戳的代次保持缺失，不追加隐藏更新轮次追齐。

case/task身份加入独立CBT参数，原CPU身份不变。manifest注明源基线SHA、GPU主证据、异步统计和捕获范围。CPU-only/GL CBT输入明确拒绝。

## 几何诊断与比较边界

小网格先检查实际GPU顶点的UV/world位置与源双线性高度，使用已存在CPU几何规则作诊断；六基础槽另有来源精确验证。它只能核对迁移/布局/几何生成，不承担主质量ground truth。

同代CPU几何oracle无法取得全部动态HeapID时，不假装完整逐叶oracle已经成立：保留来源blocking验证、实际顶点源高度检查及独立域覆盖三种互补证据。主质量始终来自捕获网格。

质量/visual与timing是独立进程；GPU原子调度未声明跨运行字节确定，不能把跨模式hash差异自动当捕获改变决策。捕获本身同代不更新验证与故障/资源代检查必须严格成立。

## 实施步骤

1. 保存CBI-02程序和短CBT timing参考，冻结本阶段test129/Peking验证case。
2. 增加独立case与记录边界、目标链接和配置拒绝检查。
3. 实现隔离捕获器，接EIP输出分支并保持CPU路径。
4. 小网格跑关键机会，核对DRAW/N/UV/高度、源blocking与质量覆盖；截图实际查看。
5. Peking有限关键帧导出并计算独立质量，核对artifact/CSV/矩阵/代次身份；不在此调面积追求质量优势。
6. 对正常捕获关闭路径做前后快验，读回成本另记；完善事实/审查并提交，再开CBI-04。

## 验收与成本

- 针对性检查：错误容量/面积/后端拒绝；CPU配置与读取仍兼容；CBT缺证据不填假N，捕获代与当前代必须相同。
- 实际GPU：固定128K/面积20/test129，必要的6基础槽和一次持续变化；Peking使用冻结返回轨迹，关键机会沿现有2/15/16/尾帧规则。
- 质量：网格哈希与evaluator一致，采样域覆盖有效，矩阵使用D3D12 ZO；不能拿当前延迟N与另一代质量配对。
- 性能：只比较捕获关闭的同配置正常路径，保存一次前后独立进程；超过工程门槛才一次复测。不以质量模式的同步时间验收性能。
- 不跑全部历史CTest，不追加容量压力、更多地形或正式统计重复。

## 风险与出口

来源可能存在可见性/深度/容量造成的质量局限，应如实记录，不通过改变面积规则修复基线。GPU布局或资源状态错误属于本阶段可修复适配问题。

大容量实际N可能超过CPU预算；报告只按实际N解释。离线文件上限明确限于最高容量加基础槽，防止任意数据导致巨量分配。

交付：同代实际捕获、独立CBT记录、可复用EIP输入及事实/审查。通过仅代表质量证据和计量链可用，不代表哪种算法质量更优。

## 实施结果与边界

同代捕获/计量验收完成；事实见 [GPU证据](../../codebase/cbt_2024/gpu_experiment_evidence.md)，审查见 [CBI-03审查](../../reviews/cbt_2024/cbi_03_gpu_evidence_review.md)。两后端构建、C++配置正反例和六项Python证据检查通过。新增CBT参数/计数独立输出，正常timing无全量捕获，CPU阶段空值已核对。

实际test129/Peking共四个mesh由独立evaluator评价，均无缺失/歧义/非法几何，hash一致。Peking第15/23机会N为36241/37299，sampled Emax为1.091344/0.927970px。128K捕获20972520字节/次，平均约7–9ms，全部单独计费。截图已实际查看。

几何oracle范围收紧：未取得全动态HeapID，不宣称逐叶CPU重建完全等价；采用来源基础槽验证、实际顶点源高度核对与独立全域覆盖互补。捕获不会改变算法轮次。

### CPU性能异常：保持打开

首次DOD50k对照41.308→60.019ms超过门槛，唯一常规复测55.877→62.297ms仍上升11.5%。增加两项定向排除而非扩矩阵：反向顺序当前47.184/旧39.766ms；使用同一路径同名exe依次替换内容，旧55.331/当前62.313ms仍上升12.6%。所有样本保留，未剔除较慢数据。

定位：增量主要分布在未修改的DOD拆分/合并阶段；常规复测分别24.048→27.029ms、12.692→14.577ms，网格hash、N、拆分/合并数量均一致。新增GPU捕获在CPU分支不调用，CSV/hash位于update计时外。现有证据不能确认是二进制布局、系统调度还是其他因素；不能因为源码未改就断言无回归，也未通过扩大重复追求通过。

本阶段状态：同代证据PASS；CPU工程性能OPEN/回归待查。此次授权范围内先完成CBI-04的有限质量比较，CPU对照固定使用保留的CBI-02程序，记录不同二进制身份，不以当前较慢DOD结果宣称CBT性能胜出。性能异常不通过修改CBT算法或质量规则处理，后续平台性能结论必须先解决该项。
