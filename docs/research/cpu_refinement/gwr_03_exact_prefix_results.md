# GWR-03 结果：精确持续前缀

> 2026-09-14。同任务及建序工作削减通过；存在已测局部修复成本上升，不称所有操作更快。

| 配置 | 完整帧 before → after (ms) | 建序 before → after (ms) | 局部 next_order before → after (ms) |
|---|---:|---:|---:|
| peking547-a-b20000-trajectory-a-timing | 32.774 → 29.772 | 3.610 → 0.826 | 0.014 → 0.111 |
| peking547-a-b20000-trajectory-c-timing | 16.195 → 11.041 | 4.097 → 0.924 | 0.018 → 0.170 |
| test129-a-b4096-trajectory-c-timing | 17.627 → 17.517 | 0.661 → 0.159 | 0.239 → 0.398 |

每配置一个进程八轮，24 个 mesh 字节一致，除账本/时间外 summary、A 决策和停止均一致。相关 transactional_lod 和独立 transactional_priority_index 通过；后者独立全排序检查跨块同分、尾部提升、删除、槽复用、增长与丢弃修复。

## 局部退化与原因

固定块重排将局部维护从少量树操作变成 h 个 256 槽读取及排序。新增计数记录的是实际这部分工作，不隐去。Peking A 定向一次复测：完整 34.728→28.320ms，局部 next_order 0.0119→0.2441ms，建序 3.6525→0.8813ms。因此退化不能当微秒噪声关闭；保留为明确取舍：当前冻结八轮完整路径仍下降，而高频稀疏局部更新可能更差。没有据此预言其他轨迹收益，也不追加第二套自适应索引。

## 成本与边界

完整树及 donorCosts map 移除；候选仍完整存在连续记录和有序块中。重建 O(n*log K)，h 块更新 O(hKlog K)，查询 O(b+rlog b)，K=256、b=ceil(n*/K)。临时 Writes 构建还有 O(wlog w) 成本；容量增长、构建/释放与 query heap 均在窗口内。Publish 仅移动预分配块和 noexcept 标量 key。键含逻辑身份，槽不是同分替代品。

该实现没有增加线程，也没有移除全 Q 投影。Peking 改善主要来自不再分配/销毁全局 ordered tree；局部更新结果不允许误写成工作量全面下降。原始 before/after/recheck 及独立二进制在 benchmark-output/cpu-refinement/gwr-03。进入 GWR-04，不继续微调块大小追求漂亮局部数值。
