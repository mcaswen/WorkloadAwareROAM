# 事务化 CPU LOD 局部续接代码事实

2026-09-14；FACT，扫描新模块全部相关文件、测试和 probe。本文件补充并更新[GTP-01 时点事实](gtp_01_serial_kernel_facts.md)；旧文件的单批限制是历史状态，不是当前限制

## 1. 文件与依赖

- Commit：新增 `PreparedTopology`、`Prepare/Publish`；`Apply` 保留为单批 oracle 包装。局部目标包括面/点/边、删面、槽位消费和稀疏活动位置变动，没有整个活动表副本
- Samples：新增 `PreparedSamples`、`Prepare/Publish`；共享 `Enumerate/Evaluate/Priority`，`StoredWeights` 避免已知归属的样本重复精确定位。完整 `Refresh` 仍只用于初始化/诊断
- Mesh：新 `TransactionalMesh.h/.cpp`，拥有公共 `TerrainMeshData`、Pending 块和代际。实验核心新增依赖已有 `parallel_roam_glm`；无生产注册或上传依赖
- Pipeline：持有 State/Samples/Mesh；`Initialize` 幂等，`Update` 直接规划后 `Apply`，后者把三个组件先共同准备再发布
- Certification：`PreservesHeight` 可选，`HeightEvidence` 只在 .cpp 暴露精确类型；提案内同批缓存不跨代际
- Types：新增高度保护标签、局部维护/输出/容量账本；`WorkLedger::Reserve` 采用几何容量增长并登记连续元素搬移
- Validation：新增独立 Samples oracle、mesh 几何检查及只应用脏区间的消费者镜像；这些不进入核心正常更新

依赖保持 `Pipeline → Commit/Samples/Mesh`。Samples/Mesh 只借用旧 State 和局部 PreparedTopology，Commit 不反向调用派生维护或 target discovery。精确 Boost 类型仍不进入公共头。Prepared 值为同步模块内受信任记录，不是接收任意外部修改的生产 API

## 2. 数据和控制流

初始化：中立当前网格 → 自有拓扑 → 全 Q 关联/评价 → 完整 CPU mesh。加载、拓扑构造、样本与 mesh 初建分别计时

每轮：当前缓存 P → 封闭 Plan → 拓扑局部准备/容量 → 样本补丁和下一全序 → 输出补丁/Pending 合并 → 最后配额检查 → 核心发布 → 样本与 mesh 无分配发布。空批保持状态代际，但下一次 Plan 会依实际 B−N 重新命名额度

样本补丁先删除旧 owner 语义；每个新面通过局部栅格包围框枚举真实 Q，和外保留 owner 按最终 face ID 比较。待所有局部评价确定，再更新新面及旧支持顶点外部 incident faces 的 P。每次有修改仍扫描活动面元数据并排序下一 raw；这部分显式 O(N log N)，与局部 Q 修复不同

## 3. 发布和输出生命周期

所有可能分配的 map 节点、向量容量、Samples/Pending 记录在发布前形成。失败可以保留容量变化，不改变已发布逻辑值。发布仅转移预分配节点、移动局部容器、写预留槽和推进一次代际；计时 map 键也提前建立

每物理面槽对应三个输出顶点，活动稠密索引引用槽。法线由当前拟合几何计算，位置/UV/高度转成公共 float 后仍核查有限性；没有重采 source 代替已改高顶点。局部活动尾交换同步产生索引 dirty 块

`Consume` 返回公共 mesh 对象借用、独立区间和代际；至下一次成功发布/reset/销毁前有效。未消费 Pending 跨更新累计，重复消费返回空区间。该原型是同步单调用者接口，未承诺并发借用或多个消费者各自追踪

## 4. 高度保护与计费

正常 v1 不加保护。保护模式对 receiver 加 donor 的完整闭支持 Q 取 old/new 最大平方高度误差；区间不相交直接判断，相交计算并缓存精确最大值，空支持拒绝。同批 receiver 集和 D_need 在 donor/保护之前冻结，保护改变后续可行/预留集合

`sample_repair` 包含 `next_order`，`reservation` 包含 `height_guard`；总 update 另计，子计时不能直接全部求和。连续扩容字节只计 vector 元素迁移，临时 map/提案/证据开销留在完整时间和逻辑记录数量中，不声称完整分配 profiler

## Unresolved / Uncertain

移动视图、动态 A、公平 A/B 成本及多线程均为后续规划。当前 Peking 原前端缺少可见证据的问题仍存在，正常轨迹 Hmax 可增长；固定视图 screen acceptance 不构成长期质量保证。全局排序及 donor order 仍可能主导小更新，局部认证也可能比实际拓扑写昂贵
