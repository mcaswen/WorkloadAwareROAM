# GWR-02 结果：结构事实与视图存储

> 2026-09-14。机制与同任务正确性通过；不是完整性能可行性通过。源码基线 114c00d，Release、观测关闭、WSL Ubuntu。每配置一个独立进程八轮，尚无统计置信区间。

| 配置 | 完整帧 before → after (ms) | 视图链 before → after (ms) | 建序 before → after (ms) |
|---|---:|---:|---:|
| peking547-a-b20000-trajectory-a-timing | 44.180 → 32.774 | 38.892 → 28.104 | 9.584 → 3.610 |
| peking547-a-b20000-trajectory-c-timing | 28.789 → 16.195 | 25.971 → 13.872 | 8.126 → 4.097 |
| test129-a-b4096-trajectory-c-timing | 18.915 → 17.627 | 2.541 → 1.806 | 1.399 → 0.661 |

24 个逐帧 mesh 字节相同；去掉时间及新增分配字段后的完整 summary 相同，A 决策/停止轨迹亦相同。transactional_lod 单一相关夹具通过，覆盖投影完成后异常、旧视图可见性、重试、局部更新、下一次换视图及独立 boundary/sample oracle。没有触发需要复测的完整时间回归。

每轨迹第一次实际换相机（round 3）分配一次备用数组，后续不分配视图数组。发布从每 Q 两字段写回变为交换指针；仍完整投影 Q 并归约闭面贡献，不能声称消除了 dense-Q。空间由原混合 SampleValue 加临时 projection 改为持久 geometry + 两个 projection；双缓冲不会在 Publish 后释放，峰值与持久驻留应同时理解。这里只报告逻辑分配/字节，不冒充 RSS 测量。

boundary 从初始真实单侧边建立；合法事务固定外接口，旧点保留属性，新内部点为 false。BuildOrders 仍重建完整有序容器、遍历 incident priority，未把这部分收益全部归因于排序。Commit 无需新增缓存更新器：原 VertexRecord 复制和新点构造已经维持该不变量。

原始基线二进制/哈希/编译配置及 before/after 全部位于 benchmark-output/cpu-refinement/gwr-02；comparison.json 保存核对和均值。快照及八轮相机不变，实际独立统计单位是进程。下一步 GWR-03 处理持续精确前缀。
