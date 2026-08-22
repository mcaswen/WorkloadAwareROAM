# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：6
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260822-215716.csv`

- 构建配置：RelWithDebInfo
- 图形后端：D3D12
- 图形适配器：NVIDIA GeForce RTX 5090 D (Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186)
- 阶段策略：最大安全并行策略
- Benchmark 标签：phase1-d3d12-dirty-upload
- Benchmark 路径：默认选项路径
- 路径采样点数：6；每种算法按相同 sampleIndex 执行完整路径
- VSync：基准测试期间已关闭

- Height map：`assets/heightmaps/Hm_Terrain_Test_129.pgm` 129x129
- Terrain size：30
- Height scale：4
- Max depth 设置：14
- ROAM 屏幕空间 split/merge 阈值：4 px / 2 px
- ROAM triangle budget：20000
- DOD 并行 Split：关闭

## 总体结果

| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6 | 19.74 | 46.54 | 18.50 | 34.64 | 10680.83 | 12591 | 36613.33 | 47464 | 105.71 | 174.50 | 1 | 14 | 14 | 0 |
| Data-Oriented CPU ROAM | 6 | 18.15 | 27.95 | 14.42 | 22.09 | 10680.83 | 12591 | 36613.33 | 47464 | 208.73 | 740.61 | 8 | 14 | 14 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 0.3714 | 0.3768 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 0.3724 | 0.1385 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 1.8989 | 2.9996 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/标记 | 0.5418 | 0.1954 | 刷新 Q_s、建堆并生成提交候选 |
| Split 拓扑 / 裂缝约束提交 | 5.6645 | 4.8532 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 3.1300 | 1.0060 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 0.8370 | 0.7233 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 12.8164 | 0.3714 | 0.3724 | 1.8989 | 0.0000 | 0.0000 | 0.5418 | 5.6645 | 0.0000 | 3.1300 | 0.8370 | 0.5630 |
| Data-Oriented CPU ROAM | 10.2929 | 0.3768 | 0.1385 | 2.9996 | 0.0000 | 0.0000 | 0.1954 | 4.8532 | 0.0000 | 1.0060 | 0.7233 | 0.1280 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 5.6645 | 5.6645 | 5.6645 |
| DoD Split | 0.1568 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 4.6952 | 4.8519 | 4.8532 |
| Merge | 0.0561 | 0.3796 | 0.0983 | 0.0037 | 1.5461 | 0.9107 | 2.9945 | 2.9996 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 9333.5000 | 1347.3333 | 660.5000 | 2115288 |
| Data-Oriented CPU ROAM | 1 | 9310.8333 | 1370.0000 | 864.5000 | 2115288 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 6.2062 | 2.2712 | 3.1300 | 0.0000 |
| Data-Oriented CPU ROAM | 5.0485 | 3.1381 | 1.0060 | 0.0000 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.01 | 0.01 | 2115288 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.01 | 0.01 | 2115288 | 0 |
