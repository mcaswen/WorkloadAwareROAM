# 运行时基准测试报告

- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径
- 每种算法的路径采样点数：64
- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法
- 采样完整性：完整
- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进
- 详细 CSV：`runtime-benchmark-20260831-035247.csv`

- 构建配置：RelWithDebInfo
- 图形后端：OpenGL
- 图形适配器：NVIDIA GeForce RTX 5090 D/PCIe/SSE2 (4.1.0 NVIDIA 591.86)
- 阶段策略：默认自动策略
- 并行拓扑候选阈值：细分 32，合并 160
- 并行拓扑限定：更新 0，阶段 both；更新 0 表示每次更新均允许
- Benchmark 标签：phase5-stress-final
- 算法顺序：Classic CPU ROAM -> Data-Oriented CPU ROAM；轮换偏移 0
- 独立预热：每种算法 4 个不计入结果的采样点，预热后重置拓扑再开始记录
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
| Classic CPU ROAM | 64 | 423.28 | 1011.88 | 405.29 | 955.27 | 200000.00 | 200000 | 1127313.41 | 1618212 | 100.12 | 126.10 | 1 | 20 | 20 | 0 |
| Data-Oriented CPU ROAM | 64 | 292.88 | 646.76 | 254.94 | 601.48 | 199999.44 | 200000 | 1127303.84 | 1618198 | 129.15 | 191.90 | 8 | 20 | 20 | 0 |

## ROAM 逻辑阶段对比

表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、活动叶预算和增量 mesh 输出语义。

| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |
| --- | ---: | ---: | --- |
| Prepare / 帧状态 | 8.5206 | 8.6130 | 准备持久拓扑和逐帧输入 |
| Merge 候选评分 | 14.7405 | 2.8925 | 刷新持久 Q_m 的优先级 |
| Merge 拓扑提交 / 向上级联 | 22.9841 | 24.6843 | 提交 diamond merge、邻接修复和级联回收 |
| Split 前 active leaf 收集 | 0.0000 | 0.0000 | 两者均直接复用持久活动叶表示 |
| 视点相关 leaf error / 视锥测试 | 0.0000 | 0.0000 | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |
| Split 扫描/评分 | 42.4325 | 7.7159 | 刷新 Q_s 分数并原地建堆 |
| Split 拓扑 / 裂缝约束提交 | 44.4543 | 38.4883 | 按同一预算与 forced-split 约束提交 |
| 细分后 active leaf 收集 / 输出视图 | 0.0000 | 0.0000 | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |
| Mesh emit / draw argument 生成 | 27.1922 | 10.1649 | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |
| Finalize / 发布 packet | 61.0755 | 30.1731 | 汇总统计、更新跨帧状态并发布 renderer packet |

## CPU 实现阶段

`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，`Split scan/mark` 只包含 Q_s 优先级刷新和原地建堆，候选快照计入拓扑规划；`Merge mark` 对应 Q_m 的同类工作。DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。

| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | Error eval | Split scan/score | Split topology | Final leaf collect/view | Mesh emit | Finalize | CPU upload |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 221.4016 | 8.5206 | 14.7405 | 22.9841 | 0.0000 | 0.0000 | 42.4325 | 44.4543 | 0.0000 | 27.1922 | 61.0755 | 6.7256 |
| Data-Oriented CPU ROAM | 122.7326 | 8.6130 | 2.8925 | 24.6843 | 0.0000 | 0.0000 | 7.7159 | 38.4883 | 0.0000 | 10.1649 | 30.1731 | 8.2529 |

### Q_s/Q_m 决策阶段语义

评分耗时只包含现有队列条目的全量分数重算，建堆耗时单独列出。队列成员由拓扑修改局部维护，其数量和耗时不计入评分刷新。候选快照属于并行辅助拓扑规划，固定串行拓扑不会生成快照。

| 算法 | 队列 | 平均条目数 | 平均评分线程数 | 评分 ms | 建堆 ms | 全量刷新 ms | 局部成员更新数 | 局部成员维护 ms | 候选快照 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | Q_m | 33158.7031 | 0.9844 | 14.4554 | 0.2852 | 14.7405 | 100627.8750 | 8.9331 | 0.0000 |
| Classic CPU ROAM | Q_s | 196875.0312 | 1.0000 | 40.8031 | 1.6294 | 42.4325 | 102656.2812 | 13.2702 | 0.0000 |
| Data-Oriented CPU ROAM | Q_m | 33158.2812 | 7.8750 | 2.6229 | 0.2696 | 2.8925 | 102192.3125 | 10.3740 | 0.0311 |
| Data-Oriented CPU ROAM | Q_s | 191668.1875 | 7.8906 | 6.5466 | 1.1693 | 7.7159 | 102655.1719 | 14.7372 | 0.0000 |

### CPU Split 拓扑阶段计时（统一口径）

六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与`Topology total` 的差值还包含上表单列的候选快照、函数调用、线程数量决策和少量循环控制开销。

| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | 活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic Split | 0 | 0 | 0 | 0 | 0 | 44.4543 | 44.4543 | 44.4543 |
| DoD Split | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 38.4833 | 38.4833 | 38.4883 |
| Merge | 0.1926 | 1.0034 | 0.3408 | 0.0084 | 4.1569 | 18.9000 | 24.6021 | 24.6843 |

### 增量 CPU Mesh 输出

`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。

| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 1 | 67408.1562 | 132591.8438 | 24350.0781 | 33600000 |
| Data-Oriented CPU ROAM | 1 | 67404.6719 | 132594.7656 | 35174.7500 | 33599832 |

### 原生 pass 包络

这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。

| Algorithm | Split | Merge | Emit | Validate |
| --- | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 86.8869 | 37.7246 | 27.1922 | 0.0000 |
| Data-Oriented CPU ROAM | 61.8110 | 27.5768 | 10.1649 | 0.0000 |

## 渲染与上传

| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 0.00 | 0.00 | 0.00 | 33600000 | 0 |
| Data-Oriented CPU ROAM | 0.00 | 0.00 | 0.00 | 33599832 | 0 |
