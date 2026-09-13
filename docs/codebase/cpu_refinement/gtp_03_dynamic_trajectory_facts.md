# 事务化 LOD 动态参考和视图刷新代码事实

2026-09-14；FACT，依据当前 Samples、Pipeline、DynamicReference、Input、Probe 与测试，更新[GTP-02 时点事实](gtp_02_persistent_state_facts.md)

## 1. 文件、接口与所有权

Samples 的 `_order` 保存 `(-P²,faceId,slot)`，`_donors` 保存 `(proxyP²,vertexId)`；另持有 donor 旧 key 查询。`Prefix/DonorPool` 只读所需前缀，完整 `Raw` 仅诊断展开。B Reservation 消费同一前缀及池，不再每批全扫/重排所有点

`PreparedSamples` 预建新增有序树节点、列出旧 key，并给出证据失效的 root/donor 身份。Publish 删除旧项并转移预分配节点。局部事务成本依赖实际受影响 P、相邻 donor 和 O(log N) 有序维护；全量初始化与换视图仍 O(Q+关联+N log N)

新增 `TransactionalDynamicReference.h/.cpp`，拥有独立 Pipeline 和本 update 内的接收/回收证据缓存。高层控制独立于 B Plan，原语、认证和 Apply 共用。首次命中 receiver 目录后缓存实际几何模板；复用时按当前 NextVertexId/rootSlot/ordinal 重新命名新点，拟合数值不改

Input 的 `Views` 仅在 probe 链接现有 FormalCamera/Manifest/CSV、TerrainLodView 与 HeightMap/STB；核心不依赖这些来源能力。两场景完整冻结相机表统一验证，再取当前场景 14..18，避免丢掉跨行校验

## 2. 证据失效依据

receiver 目录只读取 root 顶点所涉及的相邻边/面和完整 H incident support。局部几何变化会进入 target 点记录和旧/新支持；共享样本改变会使接口外 P 被重算。这些面的顶点及其 incident roots 构成保守失效集合，覆盖目录几何、样本评价和资格

donor 依赖中心 ring、环点高度与完整闭面样本。受影响 P 面的顶点、目标邻接/改高点以及删除中心均进入失效集合；不会仅以“中心没有删除”为理由保留陈旧证据。预算、当前 receiver 阈值和 pair 冲突不缓存为永久失败，每次观察重新判断

每个外部 update 清除上一轮缓存，视图及批次生命周期没有隐含跨帧重用。当前证据复用只在单线程 A 内发生；没有并发缓存读写承诺。缓存关闭的参考用于解析同轨迹 oracle，不作为性能基线

## 3. 动态控制与视图

A：当前前 64 → 第一个可执行 receiver/free 或 receiver/donor → 单事务 Apply → 局部失效 → 新全序开头。当前前缀无事务或达到 64 成功/4096 观察停止。A 汇总跨多个状态，D_need 等计数不能解释成 B 的单快照分母

`SetView` 只接受相机/分辨率变化，不允许同时更改预算、几何尺度或质量规则。私有 `PreparedView` 保存 O(Q) 的投影误差/可见标志、P 及索引；owner、reference/mesh height 和拓扑不复制。所有投影域检查完成后共同替换 view/派生值并推进代际，旧 batch 失效；相同视图不重算

Probe 的 A/B 轨迹模式执行固定八次更新，计时不做全 Q 质量诊断。完整 frame_update 包含 SetView 和本轮算法/输出维护，consume 另列。逐点恢复相对于自己离开前状态，诊断文件与计时文件的几何匹配

## Unresolved / Uncertain

本阶段仍全串行。缓存失效是有界保守邻域，未主张最小支持；相机变化仍需全 Q。有限前缀与当前接受规则没有保证长期局部恢复；A/B 是不同动态算法语义，不与 Legacy strict greediness 等价。并行执行、硬件 scaling 及生产接入均未实施
