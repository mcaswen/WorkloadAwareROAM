# GTP-02：局部续接、增量输出与高度保护对照

> 2026-09-14。依据已授权的[大规划](greedy_transactional_cpu_prototype_plan.md)，GTP-01 已以 `1414c3c` 独立提交。本阶段自主实施、验证、审查并提交后再进入 GTP-03

## 1. 目标与冻结规则

在同一自有状态上连续产生新需求、认证和提交，补齐样本、priority、CPU mesh 与 Pending 的局部维护。不得重新导入来源、复制全状态或用全量诊断充当正常续接。

正常策略冻结为 `v1 / LocalCenterRefit / no-height-guard`：原始 P 全序及前缀 64 不变；density-only / no_screen_samples 明确保留并跳过，不按 donor 可行性过滤人口。E/F 只改新点，H 仅目录指定旧中心和新点；H 支持覆盖全部受影响旧面。没有持久失败缓存，每个新批次依实际 N 重建空额度账本。

另跑 `v1 / LocalCenterRefit / local-all-Q-height-guard`，只增加完整交换支持上的 sampled Hmax 不增条件，不改变拟合解或顺序。此变体失败不否定正常持续路径。固定存活高度和前端排序改动不在本阶段引入

## 2. 文件、职责与架构判断

| 判断 | 文件 | 本阶段职责 |
| --- | --- | --- |
| Extend | TransactionalCommit.h/.cpp | 将现有局部准备与发布分开，输出只含变化记录的 PreparedTopology；旧 Apply 包装保留给独立单批 oracle |
| Extend | TransactionalSamples.h/.cpp | 公共样本枚举/评价复用；在目标局部记录上准备 owner、闭面贡献和 P 修复，发布时不分配 |
| Create | TransactionalMesh.h/.cpp | 公共 TerrainMeshData 的持久槽输出、局部 dirty 块与跨轮消费；输出职责不能堆进拓扑提交 |
| Extend | TransactionalPipeline.h/.cpp | 初始化一次派生状态；每轮组织拓扑、样本和 mesh 的共同准备/发布，不接管算法细节 |
| Extend | TransactionalCertification / Reservation / Types | 高度保护及分账，不修改现有拟合和全局需求定义 |
| Extend | 现有测试/探针与构建清单 | 追加有限连续模式、局部与全量 oracle 比对、CPU 消费验证 |

依赖：Pipeline → Commit / Samples / Mesh；Samples / Mesh 只读旧 State 和 PreparedTopology 的局部记录；Commit 不依赖 Samples / Mesh，不求新需求。Mesh 只复用公共 terrain 值类型及 GLM，不注册生产接口

`PreparedTopology` 持有删面集合、新面及槽、受影响点/边、局部活动位置变动、空闲槽消费和下一身份，不是完整目标副本。活动数组尾部搬移用稀疏位置覆盖模拟，成本随删面/新面数量计费。准备阶段允许预留 live 容量，不改变已发布逻辑值

共同事务顺序：

1. 在当前样本/P 上 Plan；得到封闭批次
2. 准备局部拓扑、目标样本/评分、mesh 写入和合并后的 Pending；所有可能失败的几何计算及分配在此完成
3. 最后核对版本及配额，然后发布预分配记录；不在发布后调用可分配的修复
4. 批次代际推进一次；Consume 是独立动作，未消费 Pending 跨轮累积

## 3. 样本、输出及成本边界

局部样本域为旧完整支持上的全 Q，加新面包围框枚举得到的闭面贡献。未删除外 owner 可复用；新 owner 以最终稳定面身份取最小值。旧支持顶点的保留 incident faces 构成外接口核查邻域，其 P 使用更新后的共享样本评价重算。旧高度变动不能遗漏这些外贡献

样本 work 显式区分位置测试、闭面贡献、评价次数与局部支持大小；正常同视图不扫描全 Q。每轮全局需求排序的元数据扫描/排序单列，可为 O(N log N)，不能写成所有续接均为 O(k)。初始化、容量增长和全量 oracle 独立计费；本阶段不优化全局 selector

Mesh 首版按物理面槽存三个顶点，稠密活动索引引用这些槽。法线来自当前拟合三角形，UV/高度同实际发布值。拓扑尾部交换只脏对应索引块，新/改高面脏顶点块；消费返回借用数据、独立区间和代际，借用至下一次成功发布/reset 有效。Pending 合并成本按未消费 dirty 块数量计费，不能通过每轮清空掩盖生命周期

高度保护先对完整交换全 Q 支持作区间比较，模糊边界精确回退。空样本和数值未知拒绝。新增保护耗时、样本工作与拒绝数量单列，不拿正常版本批宽替代保护版本证据

## 4. 最小验证和性能协议

解析测试覆盖：共享边外 owner、局部/全量样本和 P 一致；连续槽复用；不消费两轮后消费、重复空消费、CPU 脏区间镜像；准备后配额失败保持拓扑/样本/mesh/Pending；跨批闲置额度释放；全 Q 保护发现视域外高度退化

自然协议在运行前冻结：

- test129-a-b4096/sample14 与 peking547-a-b20000/sample14 各自初始化一次，同视图连续 3 轮正常更新
- 两个相同 seed 各单独跑 1 轮高度保护，作为单列保守对照
- 每配置一次诊断进程和一次计时进程，不预热、不内部重复；每轮 120 秒上限，保留失败/部分结果，不换样本
- 诊断每轮全量几何及 Samples oracle、消费镜像；全量验证不进入计时
- GTP-01 已冻结四点单批结果作行为来源对照；保存旧二进制及本阶段实际构建前后同输入短测，旧单批边界单列，新增局部维护和 mesh 成本单列，不能把少做初建称为算法加速
- 生产未改，只跑新目标；不复跑全部 CTest 或图形后端

实质回归门槛沿开发规范 `max(0.05 ms, 5%, 已知环境噪声)`，必要时一次定向复测。高度保护无批次仍如实闭环；若正常局部续接必须全域恢复或无法保持共同发布失败保证，停在本阶段审查，不推进并行平台

## 5. 实施结果

已完成，详见[结果](../../research/cpu_refinement/gtp_02_persistent_state_results.md)、[代码事实](../../codebase/cpu_refinement/gtp_02_persistent_state_facts.md)和[自审](../../reviews/cpu_refinement/gtp_02_persistent_state_review.md)。正常三轮交换 test129 为 19/14/7；Peking 空额度为 2/1/0。局部 Q/P 与全量 oracle、拓扑、输出和跨轮消费一致；保护单列，无回填旧条件

实现中按既定偶发容量边界采用几何增长，并在 WorkLedger 登记连续元素搬移，避免每次追加少量面均全量搬移。没有新增架构组件。完整成本及一次定向性能复测已记录；没有可重复的共同边界回归证据，新增局部维护和输出单列

本阶段 PASS 并独立提交后进入 GTP-03。视域外高度增长、全局 raw/donor order 成本仍是后续需测量的风险，不在本阶段声明解决
