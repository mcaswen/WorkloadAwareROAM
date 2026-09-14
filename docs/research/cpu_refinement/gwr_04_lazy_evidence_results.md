# GWR-04 结果：惰性提案与局部证据

> 2026-09-14。同任务、必要工作削减通过；不宣称已接近 Legacy。每配置一进程八轮，first 为结构候选中间记录，after 为最终实现，不删除较慢中间结果。

| 配置 | 完整帧 before → after (ms) | receiver before → after (ms) | donor before → after (ms) | reservation before → after (ms) |
|---|---:|---:|---:|---:|
| peking547-a-b20000-trajectory-a-timing | 29.772 → 27.170 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 |
| peking547-a-b20000-trajectory-c-timing | 11.041 → 10.337 | 1.554 → 1.824 | 0.235 → 0.262 | 0.055 → 0.045 |
| test129-a-b4096-trajectory-c-timing | 17.517 → 14.973 | 6.083 → 3.814 | 5.301 → 5.603 | 1.513 → 1.061 |

24 个 before/after mesh、实际 intents/attempts/exchanges、预算及 A 决策/停止一致。另两场景各八轮 B 完整诊断与 C4 正常模式 mesh 一致，独立拓扑、全 Q sample、mesh、首批反序与 Pending 检查通过。单一 transactional_lod 相关测试覆盖游标及模式对照，不重跑全集。

## 确实删除与仍然存在的工作

- 只构造真正进入 Fit 的目录项，ordinal/临时身份保持；F witness 到达 F 才形成。
- Fit 原覆盖面查找、第二次权重恢复和第三次按身份寻找覆盖面合并成一次。拟合后 Measure 复用面索引，但从 proposal 当前点读发布高度；普通 double 权重不参与区间或精确插值证明。
- 足迹按本快照提案复用。正常配对成功后不做剩余审计。D_feasible 仍精确：成功已见证局部可行，未成功必须扫完池；只有拒绝分布/pair 遍历标为 pairAuditComplete=false。
- test129 正常 pair 总数从 9088 次原完整池遍历减少；准确计数以 comparison.json 的 before/after 为准，不能把少 pair 当成少 donor 认证。

## 未采用的候选及成本修正

test129 八轮均触达 64/64 donor，Peking 唯一需要回收的轮也触达 64/64，其余为 0/0。因此惰性 donor 不具备删除认证的证据，本阶段不建立波段调度，不重复缓存现有每快照一次的 donor 结果。

第一版给单次 donor Measure 也分配覆盖证据，但通常没有复用。该候选引入多余存储，最终撤回这部分；公共 Measure 保持一次直接定位，Fit 才持有跨拟合/测量的局部缓存。first 较慢 Peking 结果保留；修正后两场景完整成本均低于紧邻前测，没有继续追小幅波动。

Reference、Interval、Rational 运算和完整接受规则均未改；FilterChecks 大量仍在，因此没有把质量认证主成本“优化完”。保守数值核、ClipPolygon、局部 Samples::Prepare 均保留。阶段空间用 EvidenceBytes 记录构造总字节，不冒充峰值 RSS。

原始 before/first/after/diagnostic、二进制及账本在 benchmark-output/cpu-refinement/gwr-04。下一步只审计一个严格的不可见样本块排除机制；不凭本阶段正向结果扩大几何/数值范围。
