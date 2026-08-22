# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：64
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260822-224155.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.1.0 NVIDIA 591.86)
- 阶段策略：默认自动策略
- Benchmark 标签：phase1-four-presets-budget-saturation-acceptance
- Benchmark 路径：极限压力路径
- 路径采样点数：64；每种算法按相同 sampleIndex 执行完整路径
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Peking_513.png` 547x547
- Terrain size：80
- Height scale：12
- Max depth 设置：20
- ROAM 屏幕空间 split/merge 阈值：0.25 px / 0.1 px
- ROAM triangle budget：200000
- DOD 并行 Split：关闭

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 64 | 390.32 | 842.88 | 387.02 | 831.38 | 200000.00 | 200000 | 1127313.41 | 1618212 | 100.36 | 106.41 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 64 | 249.93 | 984.20 | 226.88 | 515.47 | 199999.44 | 200000 | 1127303.84 | 1618198 | 132.61 | 211.55 | 8 | 20 | 20 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 8.8945 | 8.7673 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 14.5689 | 2.5020 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 20.2836 | 20.4093 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 42.2780 | 5.7981 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 39.5100 | 31.7242 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 27.9955 | 8.9199 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 57.5010 | 29.8510 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 211.0334 | 8.8945 | 14.5689 | 20.2836 | 0.0000 | 0.0000 | 42.2780 | 39.5100 | 0.0000 | 27.9955 | 57.5010 | 5.8304 |
| Data-Oriented CPU ROAM | 107.9725 | 8.7673 | 2.5020 | 20.4093 | 0.0000 | 0.0000 | 5.7981 | 31.7242 | 0.0000 | 8.9199 | 29.8510 | 6.9961 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 39.5100 | 39.5100 | 39.5100 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 31.7235 | 31.7235 | 31.7242 |
| Merge | 0.2251 | 0.9833 | 0.3420 | 0.0058 | 3.7987 | 15.0442 | 20.3991 | 20.4093 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 67408.1562 | 132591.8438 | 24350.0781 | 33600000 |
| Data-Oriented CPU ROAM | 1 | 67404.8438 | 132594.5938 | 35165.7500 | 33599832 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 81.7881 | 34.8525 | 27.9955 | 0.0000 |
| Data-Oriented CPU ROAM | 49.8524 | 22.9114 | 8.9199 | 0.0000 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 33599832 | 0 |
