# TPI-02：公共适配、种子与输入契约

> 2026-09-15，小规划；用户已授权按[大规划](transactional_platform_integration_plan.md)自主闭环。前阶段提交 `b7b4a52`；状态：公共适配及输入契约完成。

## 目标与当前事实

从公共 `TerrainLodBuildInput` 初始化一次自有事务状态，随后每调用只执行一批，并返回借用网格和增量范围。此次不接 GUI/renderer，不改变优先级、预留、高度拟合或配额内的成功轨迹。

已读开发/规划规范、TPI 大规划 §5～9、[TPI-01 事实](../../codebase/cpu_refinement/tpi_01_runtime_core_facts.md)，并核查 HeightMap、公共输入/输出、DOD 入口、家族几何导入、Pipeline、Mesh、Samples 与 Certification。两个必须补齐的事实是：HeightMap 未保留源整数；公共视图没有保存 NO/ZO，而核心投影与拟合均默认 NO。Mesh 已检查 float 有限，但尚未检查转换后的面退化。

## 归属与接口

| 判断 | 文件 | 职责 |
|---|---|---|
| Extend | `terrain/HeightMap.h/.cpp` | 保存 raw uint16、成功加载版本；本地准备完毕再发布，原 float 插值顺序不变 |
| Extend | `algorithms/ITerrainLodAlgorithm.h`、`TerrainLodView.cpp` | 保存深度约定；增加算法 ID、持续更新能力、新设置/统计值，旧默认哈希不变 |
| Create | `TransactionalLodSettings.h` | 线程、r/m、高度保护与样本配额的公开有限配置，默认 4/64/64/false/1e6 |
| Create | `TransactionalLodStats.h` | 状态码、是否执行/冷启动、当前规模与批次、阶段时间和有限工作量；不暴露 map/set |
| Wrap | `greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h/.cpp` | PImpl 拥有执行器/核心，负责 reset、一次调用、错误重试与公共返回；不复制策略 |
| Create | `TransactionalSeedBuilder.h/.cpp` | 验证输入、配置转换、一次公共串行 DOD 种子及实际 UV/高度导入 |
| Extract | `TransactionalStateInvariant.h/.cpp` | 将原独立验证器中的单状态结构核查窄提取，初建只执行一次；研究验证器转调，不搬入 mesh/sample oracle |
| Create | `TransactionalRenderBridge.h/.cpp` | 范围/统计转换和借用包构造，不规划、不读源文件、不调用上传 |
| Extend | 核心 Types/Pipeline/Samples/Certification/Mesh | 显式 NO/ZO 域及拟合系数；新写入 float 面的可表示性检查 |
| Extend | `cmake/TransactionalLod.cmake`、顶层/tests CMake | 适配库与测试链接；DOD、HeightMap、视图实现由最终入口提供，避免重复生产对象 |
| Create | `TransactionalTerrainLodAlgorithmTests.cpp`、`TransactionalRenderPacketTests.cpp` | 同种子直接核心/适配、生命周期、深度与消费故障定向核查 |

公开适配头仅包含公共接口和 PImpl；核心继续不依赖 DOD/研究。SeedBuilder 依赖 DOD 公共类，且只从初始化入口调用。StateInvariant 依赖核心与几何谓词；它不构造目标、不维护候选、不进入正常更新。RenderBridge 只做 O(范围数+有限统计字段) 转换。

## 冻结行为

- `DodBootstrapCurrentViewV1`：临时 DOD 对当前视图恰好一次 `BuildRenderData`，阶段动作与线程全部设为串行。按实际公共 UV、Position.y 精确导入与方向规范，源 raw 数据复制一次；不重放历史、不迭代到饱和。种子/完整初建与销毁成本单列。
- 支持方形至少 2×2、高度尺度为正、地形尺度正数、预算 2～200k；最大验证范围不是无限容量承诺。r/m 首版限 1～640、线程 1～32；固定资源配额与无界内存申请前的尺寸检查明确拒绝不支持输入，源边长最大 1025、种子深度 1～20。每次调用的故障时间保护为 180 秒，不按帧时预算提前接受部分结果。
- 原始资产成功加载有单调版本；配置核对只看身份、版本和固定设置，不逐帧扫资产。替换源/尺度/预算/种子设置/r/m/线程/保护/配额触发重建，相机/尺寸/深度仅 SetView。
- 普通调用 `SetView → Update（一次）→ ConsumeMesh → 包/统计转换`；初建结果和该次第一批均计入冷调用，后续不重建。Batch 和账本的正常释放包含在完整 CPU 时间。
- 0 drawable 暂停更新，有旧合法网格时可返回它；无旧状态不伪造 1×1 相机。旧视图构造器的 clamp 行为为兼容保留，renderer 后续必须在真实 0 尺寸时先暂停。
- 普通准备/数值/配额失败保留最后完整几何，并冻结同一失败输入的自动重试。视图变化可再次尝试输入/数值失败；执行器入队失败须 Reset 后恢复。reset 后初建失败没有合法新 mesh，不把旧预算结果当作新状态。
- 桥接在 Consume 后失败时，下一次有效包要求全量；renderer 资源失败的恢复标志留给 TPI-03/04。适配器提供显式请求全量同步入口，不通过清空 Pending 或重导入核心补救。
- 深度开关参与核心视图失效与公共输入身份：默认 NO 保留旧哈希，ZO 追加显式标记。NO 近面 `z+w≥0`，ZO `z≥0`；双线性参考可见人口、被测近面、区间/有理验证、拟合关于自由高度的系数均同步改变。
- 只在初建/新写入面转换时检查 float 有限、非退化与方向，使用实际拟合高度和面法线；同一点确定转换，不逐帧全域重扫。
- 阶段模型明确为事务化；旧五阶段字段不填伪造数值，`CpuWorkerCount` 不填请求线程冒充实际参与数。正常路径不开 Diagnostics 尾扫。

## 步骤和验证

1. 保存前阶段的核心/DOD 可执行基线；实现源输入、NO/ZO 和一次种子，先运行自然初建与解析域检查。若当前视图种子不满足结构契约，停止并保留反例，不偷偷改种子协议。
2. 实现适配与桥接，添加任务失败/范围转交/重置边界检查。测试使用同一个中立种子和矩阵对照直接核心与适配的 B/C 输出。
3. 必要 CTest：新增适配/范围测试、原事务核心与公共视图/投影相关测试。覆盖静止继续、空/非空批、预算降低、同路径重载、非法矩阵、NO/ZO 正交与透视近面、float 退化拒绝、分配失败及丢范围恢复；不重跑旧自然矩阵。
4. 性能：直接核心/适配相同 test129 短四次调用，分别记录冷/暖及借用范围；DOD4 和核心 C4 各复用 TPI-01 的同条件 Peking 八轮命令做当前前后对照。每配置一进程，只有新明确风险才定向复核。新增 raw 数组为 HeightMap 2WH 字节，核心自有源副本另列；不把这部分内存省略。
5. 文档、审查与提交后进入 TPI-03。分配故障注入复用 `tests/AllocationFailure.h`，仅单入口测试目标包含；原执行器测试同步使用它，生产源码不链接该支持。原始数据存 `benchmark-output/cpu-refinement/tpi-02/run-01/`，采集初限 10 分钟，单进程 180 秒/RSS 8GiB；构建与离线检查单列。旧 TPI-01 C4 时序波动作为比较限制保留。

## 出口

公共调用能够从真实源独立持续运行，借用和重置/失败契约有定向证据；深度约定不能只改最终 if。性能胜负不代替正确性，冷启动不能隐藏；当前阶段仍不证明持续质量、图形可用性或优于 Legacy。

## 实现结果

已完成上述公共值契约、一次种子、独立适配、输出桥接、源版本与 NO/ZO 实现。单状态验证从研究层窄提取，正常更新不调用它；测试故障支持不进入生产库。

test129 当前视图种子 3619 面，Peking 当前视图种子 3233 面，均通过初始结构核查。test129 同种子的直接核心与公共适配在 1/4 线程下四次输出一致。静止续接、预算降低、同路径重载、零尺寸暂停、坏视图重复抑制、分配失败恢复及浮点输出拒绝已有定向检查；公共视图与投影相关检查完成。非空范围测试使用解析事务，不能把自然静止空批误作交易性能覆盖。

核心 C4 与旧 DOD4 的八轮前后各一进程对照，16 份网格逐字节一致；核心摘要除时间外一致。移动均值 C4 22.389→22.612ms，DOD4 16.963→13.709ms。DOD 静止均值 6.402→7.211ms，单短序列和前阶段波动不支持稳定倍率；没有按这些值改算法或扩大矩阵。详细成本与限制见[事实记录](../../codebase/cpu_refinement/tpi_02_public_adapter_facts.md)，[架构审查](../../reviews/cpu_refinement/tpi_02_public_adapter_review.md)无阻塞项。下一步 TPI-03，尚未完成图形接入、持续质量或性能可行性验收。
