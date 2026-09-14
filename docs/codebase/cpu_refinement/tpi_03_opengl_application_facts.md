# TPI-03 应用与 OpenGL 接入事实

日期：2026-09-15。依据[小规划](../../plans/cpu_refinement/tpi_03_opengl_application_plan.md)，前版本 `2341710`。

## 模块与数据流

`TerrainLodAlgorithmRegistry.h` 保存名称、可用性和解析；`.cpp` 是唯一生产工厂，三算法独立拥有状态。两个 renderer 与 headless 原工厂已转调。关闭 runtime 宏时第三算法返回不可用，旧 all 保持 Classic/DOD。公开名称解析不链接重型核心。

`RuntimeBenchmarkConfig` 新增显式名单、交互算法、预算与事务配置。`ApplicationCommandLine` 接收 `--algorithm transactional`、`--runtime-benchmark-algorithms classic,dod,transactional`、`--transactional-workers`、`--transactional-prefix`、`--transactional-donors`、`--runtime-benchmark-budget`；名单不允许重复或不可用算法。启动名单默认仍两算法。GUI 通过 PanelState → RenderSettings 搬运配置，暂停不参与算法身份，单步/重置请求由 Application 消费一次。

`TerrainRenderer::RebuildMesh` 对新算法仅标记待更新；首个有效 UpdateForView 才构造种子，不用默认单位矩阵，也不在 ApplySettings 后偷跑一批。算法能力 RequiresContinuousUpdate 绕过旧静止缓存，暂停/零 drawable 不运行批次。Application 在真实 drawable 为零时暂停渲染机会；旧视图构造器 clamp 保留兼容。

`Reset/Shutdown` 先解除借用再释放实例。Build 前解除旧借用；失败如果无新合法包则清绘制数量，如果仍返回最后完整几何则上传该输出并保留错误状态。OpenGL 独立持有 `_cpuUploadRecoveryRequired`，Consume 之后上传失败，下一次传最新完整网格。缓冲错误时容量标记归零重新分配，不要求算法重新产生已经消费的脏范围。GL 错误查询限定于第三算法，旧上传边界保持。

`TransactionalPlatformProbe` 仅由 `--transactional-platform-check` 显式执行，使用现有 Window/Backend/Renderer 公共接口，负责固定生命周期序列和有限断言。普通热路径没有该序列、全网格复制或几何全量检查。TPI-04 复用入口检查 D3D12，当前只证明 OpenGL。

`TerrainLodExperimentCsv` schema 5 追加全部有限 TransactionalStats、stageModel/status；新算法旧堆/五阶段/实际线程字段为 `n/a`。公共 CPU 总时间、上传字节、mesh 范围等保留有效值。RuntimeBenchmark 对新算法单列事务/冷启动，不将其加入旧语义细分表。普通 headless smoke 对新算法检查公共输出数量、有限位置、索引、预算与状态，不套用 ROAM 稠密叶/堆 oracle；严格 profile 显式拒绝。

## 构建与验证

原生 Windows MSVC RelWithDebInfo，OpenGL 4.1、NVIDIA GeForce RTX 5090 D/591.86。所有命令由 Ubuntu 调起。Boost 1.90 通过显式 WSL UNC 路径提供；开启 runtime、关闭 tests 的完整 app 已构建。`transactional-platform` 预设允许使用 `ROAM_BOOST_INCLUDE` 指定本地依赖。不开新旗标的旧默认路径不变。

解析开/关配置及 CSV 列宽/非适用检查通过。新枚举的严格 topology profile 返回 2。有限图形检查输出包括：种子 3619 面；静止 sequence 2→3；暂停仍 3，单步 4；资源恢复全量 607992 字节；GL 错误后仍恢复同字节；resize 无字节更新；预算降为 512 后冷启动，重载同源再次冷启动，最后返回 DOD。初建合法性沿用 TPI-02，未重复运行全测试矩阵。

普通默认相机八帧新算法 12591 面，第一调用约 667.901ms（种子 31.929、核心初始化 633.996），后七次约 4.19～5.49ms；交换和自由细化均为零。因此仅是应用持续调用证据，不能解释为低成本兑现 refinement 或优于 DOD。

## 性能回归分析

原始数据：`benchmark-output/cpu-refinement/tpi-03/run-01/`。before/fixed 对照同平台默认八帧、每算法独立初始状态、无预热、旧两算法名单；开发统计单位是进程，帧不是独立重复。保留所有中间版本，未删除慢帧。

| 版本 | DOD CPU 均值 ms | DOD 上传均值 ms | DOD LOD 包络 ms |
|---|---:|---:|---:|
| 修改前 | 11.693 | 0.551 | 16.711 |
| 初版 | 13.766 | 14.059 | 32.588 |
| 反序复核初版 | 10.244 | 13.082 | 27.699 |
| 反序复核旧版 | 11.730 | 0.562 | 16.938 |
| 修复后 | 14.757 | 0.550 | 21.172 |

定位：新增无条件 glGetError 在旧上传也运行，上传第二帧出现约 100ms 停顿，两次均复现；限定到新实验路径后 DOD 上传恢复。这是接入新增的实际回归，在已批准“旧参考不被实验拖慢”的边界内修复。尚未进一步定位驱动内部停顿，不能把推测当作精确 GPU 归因。第三算法仍承担此错误查询，TPI-05 必须计入，不能排除以制造收益。

before/fixed 的 16 行面数、拓扑/叶/mesh 哈希、事件与上传字节完全一致。CPU 均值跨版本方向变化，短序列约 10～15ms，不据此承诺稳定性能等价，也不继续追微差。Classic 最终上传约 0.542ms，旧版约 0.336～0.386ms；没有字节或算法工作增加证据，正式统计尚未进行。

## 当前边界

OpenGL 生命周期接入通过；自然实际交易覆盖不足；连续质量与竞争性性能开放。D3D12 仅工厂已共享，资源时序待 TPI-04。窗口 resize 和错误恢复证明消费者可继续，不证明任意相机都能成功或算法已达到理想质量。
