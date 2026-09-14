# GWR 最终结果：工作削减有效，性能竞争力仍未成立

> 2026-09-14。已完成 GWR-01～06 的限定工作：保留结构事实/视图缓冲、精确分块前缀、惰性目录/局部证据/配对审计分离；不可见块候选没有削减目标工作，已撤回。未接平台、未增加线程/原语、未修改质量规则、未提交 Git。

## 1. 最终工程结果

| 场景 C4 | 原版均值 ms | 最终均值 ms | 时间下降 | 当前 DOD ms | final / DOD | 5×距离线 |
|---|---:|---:|---:|---:|---:|---|
| test129-a-b4096 | 18.915 | 15.037 | 20.5% | 0.612 | 24.59× | 未达 |
| peking547-a-b20000 | 28.789 | 11.226 | 61.0% | 2.858 | 3.93× | 进入距离线内 |

这不是同任务并行 speedup：新算法 C4 与 DOD 默认 8 线程不同，决策/质量任务也不同。工程线只决定是否还有有限投入依据，不能替代等质量、同资源或论文竞争力验收。test129 仍差约 25 倍；Peking 虽进入 5×距离线内，仍显著慢于 DOD。不能继续预设该实现会绝对取胜。

每个主数字是一进程八个不同视图的均值；相机固定为 14,14,14,15,16,17,18,14，独立统计单位是进程，不把八帧当八次重复。不同阶段和最终时刻有机器波动，不宣称统计显著或稳定尾延迟。

## 2. 逐帧与变更历史

| 场景 | round/view | 原版 ms | 最终 ms |
|---|---|---:|---:|
| test129-a-b4096 | 0/14 | 36.277 | 26.856 |
| test129-a-b4096 | 1/14 | 21.656 | 19.286 |
| test129-a-b4096 | 2/14 | 14.431 | 13.278 |
| test129-a-b4096 | 3/15 | 17.871 | 13.911 |
| test129-a-b4096 | 4/16 | 16.447 | 11.899 |
| test129-a-b4096 | 5/17 | 14.630 | 11.544 |
| test129-a-b4096 | 6/18 | 15.927 | 12.843 |
| test129-a-b4096 | 7/14 | 14.078 | 10.679 |
| peking547-a-b20000 | 0/14 | 9.121 | 7.033 |
| peking547-a-b20000 | 1/14 | 1.870 | 2.148 |
| peking547-a-b20000 | 2/14 | 1.796 | 1.990 |
| peking547-a-b20000 | 3/15 | 48.379 | 17.914 |
| peking547-a-b20000 | 4/16 | 50.760 | 15.476 |
| peking547-a-b20000 | 5/17 | 44.521 | 14.511 |
| peking547-a-b20000 | 6/18 | 37.227 | 15.295 |
| peking547-a-b20000 | 7/14 | 36.637 | 15.443 |

| 场景 | 静止后两轮原版→最终 ms | 五次换视图原版→最终 ms | 本次最大帧原版→最终 ms |
|---|---:|---:|---:|
| test129-a-b4096 | 18.043→16.282 | 15.791→12.176 | 36.277→26.856 |
| peking547-a-b20000 | 1.833→2.069 | 43.505→15.728 | 50.760→17.914 |

分阶段证据：[GWR-02](gwr_02_view_storage_results.md)、[GWR-03](gwr_03_exact_prefix_results.md)、[GWR-04](gwr_04_lazy_evidence_results.md)、[GWR-05 失败](gwr_05_view_block_results.md)。不把某一最小读数拼成最终结果；final 是撤回条件候选后的固定保留版本。

## 3. 负结果、复测与未决成本

- GWR-03 的局部索引修复从少量树维护变为 h 个 256 槽块重排，明确放大稀疏局部工作。Peking A 一次阶段复测：next_order 0.0119→0.2441ms，完整帧 34.728→28.320ms。保留的是当前完整轨迹取舍，不是“局部回归已修复”。
- 最终边界复测仅针对初始化/静止轮疑点，没有扩大实验矩阵：
  - test129-a-b4096：Samples 初始化 146.204→176.064ms；完整均值 18.862→15.509ms；静止 round1/2 均值 18.187→16.521ms。
  - peking547-a-b20000：Samples 初始化 311.971→332.610ms；完整均值 29.506→11.950ms；静止 round1/2 均值 1.898→1.992ms。

test129 初建增加约 30ms 在两次观察中方向一致，属于确认存在的启动成本问题。当前只定位到 Samples::Refresh 边界，不能将“分离数组导致访存变化”当作已证原因；具体子函数因果尚未定位。Peking 初建/静止轮波动较大，不能宣称精确退化百分比。此次已有必要复测，不再扩 profiler/采样追数。

这些成本保留为未决项，不因另一场景收益而关闭。当前主要门槛是持续帧可行性，保留候选不代表所有性能验收通过；若后续要求低启动延迟或高频静止更新，必须先 Review 本取舍。没有未经授权追加新修复规划。

GWR-05 的块界没有有效排除：Peking 0/8954405，test129 5/494085。全部运行接线及专有计数/测试已撤回，失败源码与可执行文件先归档。此结果只否定本轮单一块界候选，不证明所有层次查询不可能。

## 4. 账本：减少了什么，没减少什么

以下为八轮累计，不是每线程 CPU time。ReceiverConstructed 是实际构造，Proposals 是实际 Fit；DonorCertified 字段统计 Donor 准备/认证调用，包含失败，不是认证通过数。

| 场景 | 原版→最终 Project 调用 | 原版→最终 pair 查询 | 实际构造/Fit | 覆盖事实首次建立/复用 | donor 调用/不同必要触达 | 足迹构造 |
|---|---:|---:|---:|---:|---:|---:|
| test129-a-b4096 | 506082→506082 | 9088→7316 | 2693/2693 | 16288/11508 | 512/512 | 352 |
| peking547-a-b20000 | 8955073→8955073 | 64→64 | 3103/3103 | 98/92 | 64/64 | 53 |

旧版没有独立统计实际构造的完整目录项，不能虚构其精确数量。可确认当前构造数等于实际 Fit 次数，不再构造未试尾部。仍有 512/64 次 donor 准备，必要搜索触达全池，没有 donor 懒认证收益，因此没有新增调度。

普通模式成功后不再遍历当前需求剩余 pair；D_feasible 仍是精确存在性计数，只有拒绝分布不完整。pairAuditComplete=false 清楚标记此区别。区间/有理算术、动态近面检查和实际舍入后质量接受仍执行。没有把 debug-only 验证的时间当作可回收生产时间。

## 5. 空间、初始化和生命周期

本次 ABI 实测 SampleValue=40B、SampleGeometry=32B、SampleProjection=16B，见 sizes.cpp/txt。原稳定样本区约 40q，新双缓冲稳定区约 64q：test129 增加约 2.26MiB、Peking 约 40.99MiB。原换视图已有临时投影，故投影相关峰值增加约 8q，即 0.75/13.66MiB，另加索引和局部证据。不是“零复制所以零空间”。

一次实际换视图分配备用，其后复用；初始化当前及备用第一次分配均计费。View 发布不再逐 Q copy-back，但所有 Project/闭面归约仍在。EvidenceBytes 是逐提案分配总量，不是峰值；完整 RSS/硬件 cache traffic 本轮未测。

| 场景 | 索引重建块/访问槽 | 索引比较/查询块首 | 局部证据累计字节 | 容量预留/搬移字节 |
|---|---:|---:|---:|---:|
| test129-a-b4096 | 294/73339 | 451789/200 | 1864720 | 640/0 |
| peking547-a-b20000 | 634/159715 | 1511538/952 | 5160 | 10455832/6970352 |

## 6. 最终 Tracy：互斥墙钟阶段与实际并行边界

每场景一份八帧捕获，均 complete，真实执行线程数为 4。下表只相加互斥主路径阶段；SetView 包含投影/评分/索引，不再把它们叠加。此为观测开启下的时序归因，不是正式性能结果。

| 场景 | 阶段 | 八帧 ms | 整体帧占比 |
|---|---|---:|---:|
| test129-a-b4096 | SetView | 5.699 | 5.14% |
| test129-a-b4096 | receiver dispatch | 26.562 | 23.96% |
| test129-a-b4096 | donor dispatch | 41.502 | 37.44% |
| test129-a-b4096 | reservation | 9.278 | 8.37% |
| test129-a-b4096 | local Samples::Prepare | 21.587 | 19.47% |
| test129-a-b4096 | Commit::Prepare (含 face fill) | 3.205 | 2.89% |
| test129-a-b4096 | Commit::Publish | 0.467 | 0.42% |
| test129-a-b4096 | Mesh::Prepare (含 mesh fill) | 0.604 | 0.54% |
| test129-a-b4096 | Samples/Mesh publish | 0.562 | 0.51% |
| test129-a-b4096 | 其他控制/空隙 | 1.386 | 1.25% |
| peking547-a-b20000 | SetView | 68.275 | 78.81% |
| peking547-a-b20000 | receiver dispatch | 12.163 | 14.04% |
| peking547-a-b20000 | donor dispatch | 1.934 | 2.23% |
| peking547-a-b20000 | reservation | 0.393 | 0.45% |
| peking547-a-b20000 | local Samples::Prepare | 1.449 | 1.67% |
| peking547-a-b20000 | Commit::Prepare (含 face fill) | 0.473 | 0.55% |
| peking547-a-b20000 | Commit::Publish | 0.032 | 0.04% |
| peking547-a-b20000 | Mesh::Prepare (含 mesh fill) | 1.554 | 1.79% |
| peking547-a-b20000 | Samples/Mesh publish | 0.032 | 0.04% |
| peking547-a-b20000 | 其他控制/空隙 | 0.331 | 0.38% |

test129 的接收/回收仍占大头，局部 Samples::Prepare 成为约五分之一的完整帧成本；Peking 的 SetView 仍接近八成。短 face/mesh fill 仍可能由同一 worker 消费；这不是缺乏任务实现的证据，而是任务很短。pool.wait 包含等待工作完成，不能作为纯同步税与任务墙钟再相加。

视图五次实际更新的内部时序：

- test129-a-b4096：投影派发 2.840ms，评分派发 1.788ms，索引 1.015ms，视图 Publish 0.013ms。
- peking547-a-b20000：投影派发 32.814ms，评分派发 27.843ms，索引 7.348ms，视图 Publish 0.168ms。

完整函数调用/时序另表（同线程嵌套及跨线程汇总，不可求和成帧时间）：

| 场景 | 函数 zone | 次数 | inclusive ms | self ms |
|---|---|---:|---:|---:|
| test129-a-b4096 | gtp.accepts | 6729 | 0.2632 | 0.2632 |
| test129-a-b4096 | gtp.apply | 8 | 27.0496 | 0.6250 |
| test129-a-b4096 | gtp.commit.prepare | 8 | 3.2048 | 2.5551 |
| test129-a-b4096 | gtp.commit.publish | 8 | 0.4672 | 0.4672 |
| test129-a-b4096 | gtp.dispatch | 42 | 73.8877 | 0.1110 |
| test129-a-b4096 | gtp.donor | 512 | 117.6807 | 9.6748 |
| test129-a-b4096 | gtp.fit | 2693 | 56.4854 | 48.0722 |
| test129-a-b4096 | gtp.initialize | 16 | 0.0053 | 0.0053 |
| test129-a-b4096 | gtp.ledger_merge | 42 | 0.0537 | 0.0537 |
| test129-a-b4096 | gtp.measure | 614 | 116.4142 | 116.4142 |
| test129-a-b4096 | gtp.mesh.prepare | 8 | 0.6040 | 0.0589 |
| test129-a-b4096 | gtp.mesh.publish | 8 | 0.0344 | 0.0344 |
| test129-a-b4096 | gtp.plan | 8 | 78.0486 | 0.7065 |
| test129-a-b4096 | gtp.receivers | 3063 | 9.3195 | 9.3195 |
| test129-a-b4096 | gtp.reservation | 8 | 9.2779 | 9.0195 |
| test129-a-b4096 | gtp.samples.orders | 5 | 1.0147 | 1.0147 |
| test129-a-b4096 | gtp.samples.prepare | 8 | 21.5870 | 21.5870 |
| test129-a-b4096 | gtp.samples.prepare_view | 5 | 5.6647 | 0.0213 |
| test129-a-b4096 | gtp.samples.publish | 8 | 0.5272 | 0.5272 |
| test129-a-b4096 | gtp.samples.publish_view | 5 | 0.0134 | 0.0134 |
| test129-a-b4096 | gtp.set_view | 8 | 5.6989 | 0.0161 |
| test129-a-b4096 | gtp.task | 168 | 197.2382 | 13.7526 |
| test129-a-b4096 | gtp.update | 8 | 105.1147 | 0.0159 |
| peking547-a-b20000 | gtp.accepts | 64 | 0.0619 | 0.0619 |
| peking547-a-b20000 | gtp.apply | 8 | 3.5964 | 0.0566 |
| peking547-a-b20000 | gtp.commit.prepare | 4 | 0.4732 | 0.2105 |
| peking547-a-b20000 | gtp.commit.publish | 4 | 0.0324 | 0.0324 |
| peking547-a-b20000 | gtp.dispatch | 34 | 75.2646 | 0.1115 |
| peking547-a-b20000 | gtp.donor | 64 | 6.3017 | 1.2371 |
| peking547-a-b20000 | gtp.fit | 3103 | 24.3473 | 24.1982 |
| peking547-a-b20000 | gtp.initialize | 16 | 0.0024 | 0.0024 |
| peking547-a-b20000 | gtp.ledger_merge | 27 | 0.0520 | 0.0520 |
| peking547-a-b20000 | gtp.measure | 64 | 5.1556 | 5.1556 |
| peking547-a-b20000 | gtp.mesh.prepare | 4 | 1.5539 | 1.3056 |
| peking547-a-b20000 | gtp.mesh.publish | 4 | 0.0046 | 0.0046 |
| peking547-a-b20000 | gtp.plan | 8 | 14.7157 | 0.2263 |
| peking547-a-b20000 | gtp.receivers | 3605 | 7.2330 | 7.2330 |
| peking547-a-b20000 | gtp.reservation | 8 | 0.3926 | 0.3888 |
| peking547-a-b20000 | gtp.samples.orders | 5 | 7.3479 | 7.3479 |
| peking547-a-b20000 | gtp.samples.prepare | 4 | 1.4485 | 1.4485 |
| peking547-a-b20000 | gtp.samples.prepare_view | 5 | 68.0807 | 0.0760 |
| peking547-a-b20000 | gtp.samples.publish | 4 | 0.0271 | 0.0271 |
| peking547-a-b20000 | gtp.samples.publish_view | 5 | 0.1680 | 0.1680 |
| peking547-a-b20000 | gtp.set_view | 8 | 68.2754 | 0.0257 |
| peking547-a-b20000 | gtp.task | 108 | 235.9821 | 198.1002 |
| peking547-a-b20000 | gtp.update | 8 | 18.3260 | 0.0124 |

## 7. 最终 perf：函数热点

各场景固定 17 次同种子重放，只用于函数采样，初始化/JSON 在 ROI 外；不是 17 组速度统计。两个捕获均达到初筛样本量门槛，无 lost、missing-stack、unknown-self。仍有大量 worker 根部 unknown ancestor，不能称完整调用树已无缺口；这里依赖机器函数 self，调用关系和同步边界由 Tracy 补充。

- test129-a-b4096：2059 ROI 样本，1750 样本含未知祖先；136 个握手窗口。
- peking547-a-b20000：2074 ROI 样本，1992 样本含未知祖先；132 个握手窗口。

| 场景 | 机器函数（简写） | self 占比 | 自身样本 |
|---|---|---:|---:|
| test129-a-b4096 | __nextafter | 22.10% | 455 |
| test129-a-b4096 | Boost eval_gcd | 5.25% | 108 |
| test129-a-b4096 | Interval operator* | 5.25% | 108 |
| test129-a-b4096 | Boost divide_unsigned_helper | 3.16% | 65 |
| test129-a-b4096 | TransactionalSamples::Weights | 3.01% | 62 |
| test129-a-b4096 | Boost operator= | 2.91% | 60 |
| test129-a-b4096 | Boost do_assign_float | 2.57% | 53 |
| test129-a-b4096 | __frexpl | 2.43% | 50 |
| test129-a-b4096 | __wrap_scalbnl | 2.38% | 49 |
| test129-a-b4096 | TransactionalSamples::Prepare | 2.23% | 46 |
| test129-a-b4096 | Boost eval_multiply | 2.14% | 44 |
| test129-a-b4096 | TransactionalSamples::Project | 1.89% | 39 |
| test129-a-b4096 | __memmove_avx512_unaligned_erms | 1.70% | 35 |
| test129-a-b4096 | Boost eval_left_shift | 1.70% | 35 |
| test129-a-b4096 | TransactionalState::Vertex | 1.55% | 32 |
| test129-a-b4096 | TransactionalSamples::VisibleSupport | 1.51% | 31 |
| test129-a-b4096 | Boost eval_gcd | 1.46% | 30 |
| test129-a-b4096 | __scalbnl | 1.41% | 29 |
| test129-a-b4096 | Boost eval_add_subtract_imp | 1.41% | 29 |
| test129-a-b4096 | Samples::PrepareView / view_scores | 1.36% | 28 |
| test129-a-b4096 | std::array< | 1.31% | 27 |
| test129-a-b4096 | Boost divide_unsigned_helper | 1.26% | 26 |
| test129-a-b4096 | void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsigned int> > >, long, __gnu_cxx::__ops::_Iter_less_iter> | 1.21% | 25 |
| test129-a-b4096 | Samples::PrepareView / view_projection | 1.17% | 24 |
| test129-a-b4096 | Boost boost::multiprecision::backends::cpp_int_base<0ul, 18446744073709551615ul, (boos | 1.07% | 22 |
| test129-a-b4096 | nextafter@plt | 1.02% | 21 |
| peking547-a-b20000 | Samples::PrepareView / view_scores | 27.53% | 571 |
| peking547-a-b20000 | TransactionalSamples::Project | 24.93% | 517 |
| peking547-a-b20000 | Samples::PrepareView / view_projection | 21.50% | 446 |
| peking547-a-b20000 | TransactionalState::Vertex | 3.28% | 68 |
| peking547-a-b20000 | TransactionalSamples::Decode | 2.80% | 58 |
| peking547-a-b20000 | TransactionalSamples::BuildOrders | 1.98% | 41 |
| peking547-a-b20000 | Boost eval_gcd | 1.54% | 32 |
| peking547-a-b20000 | __nextafter | 1.40% | 29 |
| peking547-a-b20000 | __wrap_scalbnl | 1.16% | 24 |
| peking547-a-b20000 | Boost do_assign_float | 1.11% | 23 |

少于 30 个自身样本仅为弱线索；同名 Boost 简写可能对应不同重载，完整名字保留在 perf-self-functions.csv。Peking 前三项约 74% 的自身样本仍在密集投影/评分；不能把 lambda 函数名误读成单纯 std::function 派发成本。test129 的 nextafter 和区间乘法、精确算术仍显著；它们承担实际数值认证义务，本轮未证明可以删除。

## 8. 复杂度与理论边界

沿用 n/N/q/a/d/r/m/b、s_i、s_delta、z 的定义，详见已同步的[算法页](transactional_algorithm_overview.md)。K=256，Bidx=ceil(N/K)，h 为脏块数，X 为变长精确算术，Lambda 为实际容量搬移。

| 部分 | 保留实现的工作边界 | 不能省略的成本 |
|---|---|---|
| View | O(q+a+N+n log⁺N)，投影发布 O(1) | 全 Q 投影、闭面贡献、节点映射访问与旧索引销毁 |
| 全量候选索引 | O(N log⁺K+nd) | 连续 key/块构造，不是无初始化分配 |
| 精确前缀 | O(Bidx+(r+m)log⁺Bidx) | 扫块首与临时合并堆；不宣称 O(r) |
| 局部索引 | O(wlog⁺w+hKlog⁺K)+Lambda | 写集与完整脏块，稀疏更新确实可能更贵 |
| 接收目录 | 只对实际尝试项构造，最多 rc | F witness、support 样本收集仍可很大 |
| Fit + certificate | 每提案 O(s_i d log⁺N+s_i log⁺s_i+F_i)+X | 缓存仅消重复定位；二维 F_i 保守 O(s_i³) 未改 |
| Donor | 原有限耳切与全池认证边界保持 | 需要时所有 m 都触达，不能按已执行 b 计费 |
| Reservation | 至多 rm 对，足迹从按 pair 构造改为按提案构造 | 未成功请求仍完整池、与 b 个预留冲突检查 |
| continuation / mesh | 原局部样本、归属、Pending 边界 + 索引新项 | bounded d 不限制 s_i、a_delta、z、搬移或 Pending |

没有新 work/span 定理，也没有证明剩余成本是算法 intrinsic lower bound。减少一项 CPU self 占比不能直接换成完整墙钟加速上限。当前质量任务与 DOD 不同，不把差额全部叫实现冗余或全部叫必要认证。

## 9. 正确性、质量与本轮出口

从原版到最终的 16 个 C4 逐帧 mesh 字节相同，intents、实际尝试及采用提案、预算和 exchange 完全一致。受影响 A 各阶段都有原二进制轨迹对照。最终相关两项 CTest 通过；诊断 B 与正常 C4 的 16 帧结果一致，独立 Q、拓扑、反序与 Pending 证据复用 GWR-04。没有删严格数值或语义测试来换时间。

返回帧质量（仅当前固定参考/Q，不是感知或连续质量保证）：

| 场景 | ours / DOD sampled screen max px | ours / DOD height max |
|---|---:|---:|
| test129-a-b4096 | 1.573643 / 1.573643 | 0.090196 / 0.078431 |
| peking547-a-b20000 | 0.496345 / 0.496345 | 4.112899 / 0.081263 |

没有独立质量优势补偿剩余性能差额的证据。Peking 全域高度误差显著更高，旧风险没有因性能优化关闭；返回视图 sampled maximum 相同也不能覆盖局部退化或 popping。

| Gate | 当前状态 |
|---|---|
| 本轮同任务/预算/续接正确性 | 有限验证通过 |
| 实际工作削减 | 通过：视图生命周期/排序/重复目录与定位/审计；donor 认证工作未下降 |
| GWR-05 单一块界 | No-Go，运行候选撤回 |
| 5×工程距离 | test129 未达；Peking 进入线内，仍非竞争性胜出 |
| 初始化/局部稀疏维护 | 已确认成本问题，未解决，不称无回归 |
| 端到端优于 DOD / 正式论文竞争力 | 未成立；scaling/crossover 与连续质量未验证 |

本 GWR 有限包在此关闭，暂不正式接平台，不增加线程或原语，也不自动展开第二个误差研究。后续若继续，应先 Review 剩余 runtime quality cost 与启动/局部维护取舍，不能继续预支“必然更快”。

## 10. 可追溯产物

- 原版 114c00d、各阶段独立二进制和 before/after：benchmark-output/cpu-refinement/gwr-02～05。
- 最终源码/普通二进制/哈希及三种 CMakeCache：benchmark-output/cpu-refinement/gwr-06/source、final-bin、final-identity.json。Git 工作区未提交，身份同时记录 source hash，不能只引用 HEAD 假装是已提交版本。
- 关闭观测计时、DOD、边界复测：同目录 final、dod、boundary-recheck。八帧不是重复样本；不删除慢轮。
- perf/Tracy：同目录 profiling/<scene>-<tool>/manifest.json 与 artifacts；全部捕获 complete，原始文件先写 Linux 缓存，所记录二进制保留。完整符号为 perf-self-functions.csv。
- 本页及 comparison.json 由同目录 analyze.py 生成；临时分析和原始数据被 Git 忽略，正式结论页纳入文档。
