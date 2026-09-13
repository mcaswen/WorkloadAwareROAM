# GTP-03：动态串行参考与有限相机轨迹

> 2026-09-14。GTP-02 已以 `749d48f` 提交；按已授权[大规划](greedy_transactional_cpu_prototype_plan.md)继续自主闭环。本阶段不启动线程或生产接入

## 1. 冻结比较任务

正常配置仍为 v1 / LocalCenterRefit / no-height-guard。A 是一般网格原语上的 dynamic serial priority greedy，并非 Classic/DOD 或严格 ROAM 的替身；B 仍为已冻结 snapshot greedy reservation。A/B 结果允许不同，时间差是反馈语义与 batching 的联合变化，不能称为多核加速

A 每个决策状态取当前完整优先全序的前 64 项；依序生成/复用同目录接收证据。有实际空额度时尝试首个合格 receiver，否则从当前低损伤前 64 donor 中取首个内部独立且满足接收阈值的 pair。找到一个事务立即提交，再从新状态最高优先开始；本次前缀无可执行事务则停止。一次外部 update 最多成功 64 个事务、最多 4096 次 root 观察，达到上限报告 cap，不能声称收敛。无额外阈值、后端或样本选择

失败的几何证据可在依赖未变时缓存，但每次资源变化仍重新判断预算和 pair；不永久缓存“无 donor/无额度”。receiver 缓存覆盖目录涉及的 root 顶点完整邻域，donor 缓存覆盖 ring 和共享样本评价；变更支持的邻面/顶点精确失效。缓存新点身份在复用时重绑定本次独占新身份，不能碰撞已发布点或改变拟合值。提供关闭证据缓存的慢 oracle，只用于解析等价验证

## 2. 文件与职责

| 判断 | 文件 | 责任 |
| --- | --- | --- |
| Extend | Samples / Types | 持久 receiver 优先序和 donor 代理序，局部更新有序索引；视图变化只重算全 Q 投影及相应优先级 |
| Extend | Commit / Pipeline | 输出当前事务影响的候选身份集合；视图刷新与发布代际一致，失效旧批次 |
| Create | TransactionalDynamicReference.h/.cpp | A 的独立选择、证据缓存/失效、有限停止；复用同一原语/认证/Apply，不能调用 B 的完整 Plan 后丢弃大部分结果 |
| Extend | Input / probe / CMake | 只在探针侧复用 FormalCamera/Manifest 冻结视图，不运行 Legacy；核心仍不依赖 Formal/DOD |
| Extend | Validation / tests | 局部有序索引、view refresh、缓存开关相同决策与端点、轨迹输出 |

GTP-02 的全 raw 排序与 donor 全扫若逐事务复制到 A，会人为增加参考成本。本阶段改为 A/B 共用的 ordered set：受影响候选移除旧 key、插入新 key，费用 O(s log N)，完整 Raw 仅诊断按需展开。Samples 已拥有 P/闭面关联，故索引归其优先级缓存职责；全局预留策略仍在 Reservation，缓存策略仍在 DynamicReference，不新增通用候选框架

视图变化保持参数域 owner、reference height、mesh height 不变。先在私有 O(Q) 投影结果及 O(N) 评分/索引上准备，再发布新 view 和代际；该全域 view 成本明确计入，不能称局部维护，也不复制拓扑或 mesh。未变化视图不得重复刷新。潜在数值域失败保持旧视图/状态可用

## 3. 输入与测量协议

两场景各 sample14 初始化一次。相机顺序固定 `14,14,14,15,16,17,18,14`，共 8 次更新。相机使用既有 MPR/P5 冻结 CSV，经现有 Formal 验证构建；要求 sample14 的矩阵与既有 seed 完全一致。只加载初始 mesh，其他 sample 只消费视图，不导入 Legacy 几何

A/B 各一诊断进程、一计时进程；每 update 120 秒，保存已完成行与失败信息，不因负结果补样本。GTP-02 的两个三轮计时作为修改前基线，保存旧二进制/哈希；B 前三轮应复现相同事务/网格，新增索引若有实质回归只做一次定向复测

每轮记录：完整 update（含 view refresh 与 mesh）、单独 view/project/order、直接发现/认证/预留/准备/发布/局部维护、证据命中/失效、Q touches、候选索引修改量、N/B、实际事务和停止原因。初始化、诊断、序列化单列；严禁以 apply-only 时间称算法时间

诊断按相同 Q 报 screen max、terrain-sampled screen RMS（仍非 screen-area weighted RMS）、Hmax 与最大点；保存离开前第 3 轮的逐点误差。返回原视图时分别报告更新前/后 pointwise excess、重新可见样本误差与恢复情况，只有一次恢复更新，未恢复则右删失。无视觉可接受阈值、无长期保证

## 4. 验证与退出

解析：局部有序索引对全量 oracle；相机切换不改变 owner/mesh，过期 batch 拒绝；A 证据缓存开/关保持 root/事务/最终状态；缓存中旧身份不得复活；失败资源需求会在修改后重新判断

自然：每外部 update 全量拓扑/派生和输出核查，计时遍关闭；首三轮 B 对上阶段已冻结结果。不在 A 每个内部事务上全扫大 Q，不扩大全部 CTest

阶段出口按真实数据判断：若发现/认证/续接不能形成竞争性合理参考，或视域返回暴露当前接受契约无法处理的实质问题，记录具体层级并停止后续设施；若持续链路和成本边界清楚、仍存在值得测量的并行部分，则提交后进入 GTP-04。本阶段不能依据不同任务时间差预支 speedup

## 5. 实施结果

已完成，[结果](../../research/cpu_refinement/gtp_03_dynamic_trajectory_results.md)、[代码事实](../../codebase/cpu_refinement/gtp_03_dynamic_trajectory_facts.md)、[审查](../../reviews/cpu_refinement/gtp_03_dynamic_trajectory_review.md)分别记录。A/B 八次更新全部完成，B 前三轮与 GTP-02 网格相同，局部索引/样本与全量 oracle 一致；缓存关闭的解析参考保持同轨迹

test129 A/B 均 56 个事务，总 frame update 321.707/272.522ms；Peking 为 4/5 个事务、291.901/316.162ms。不同任务不能称线程加速。返回视图的 test excess 0.486300/1.125149px 均未在一次更新恢复，质量问题保留；Peking 零 excess 不覆盖全部视域外退化区域

执行和成本边界已清楚，test 认证与 Peking 视图阶段提供有限并行测量依据，独立提交后进入 GTP-04。保持原始接受/前缀/原语，不因恢复负结果增加实验或改变规则；没有准许生产接入
