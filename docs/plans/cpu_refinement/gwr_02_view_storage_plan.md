# GWR-02：结构事实与视图缓冲

> 2026-09-14；[GWR](greedy_transactional_work_reduction_plan.md) 授权内第二小阶段，GWR-01 已闭环。旧算法源码 `114c00d`。

## 目标、范围和设计

删除换相机时的边界邻域重查、重复视图临时分配/清零及投影结果逐 Q 复制发布。完整全 Q 投影与面归约仍执行，排序不在本阶段修改。所有优先键、样本值、事务和最终输出保持。

- Extend State/Commit：VertexRecord 保存 Boundary，初建由边关联推导；认证原语保留外边界，旧点复制原属性，新域内点为 false。degree 已由 Incident 表达。BuildOrders 直接读缓存，H 接收走同一查询；独立验证以边表检查缓存，不反向使用缓存证明自身。
- Create `TransactionalViewState.h/.cpp`：只拥有当前/备用的 `SampleProjection` 连续数组、数量和缓冲生命周期。Prepare 返回可写备用 span，稳定容量不清零；Publish 交换指针，不逐 Q 复制。首次分配计时/计字节，完整覆盖后才发布；失败丢弃可见性，保留备用内存等待下一完整覆盖。
- Extend Samples：几何 SampleValue 与 view projection 分离；新增 `SampleCount/Value` 按 ID 合成当前逻辑值，内部几何/owner 继续局部保存。迁移所有 Values 消费，不保留 stale 视图字段。PreparedSamples 仍以完整局部 Value 描述目标，发布时分别写几何与当前视图；旧备用只允许在下次全量重写后使用。
- Adapt Proposals、Certification、Validation、Probe、Tests 的样本读取；Types/Execution/Probe 增加视图分配次数/字节。Pipeline 的 PrepareView 由 const 变为有所有权的备用准备，控制与公开算法规则不变。

新 ViewState 不依赖 State、Samples 或 Pipeline，仅借 WorkLedger 和样本槽类型；声明与实现独立，避免 Samples 再承担缓冲细节。不加全局缓存、不删除动态谓词、不新建运行策略开关。

## 验证与成本

先构建原观测关闭 Release 三个相关可执行文件，归档到 `benchmark-output/cpu-refinement/gwr-02/baseline-bin`，保存 SHA/命令/编译配置。WSL 存活会话串行运行两场景各一进程 `trajectory-c-timing`，以及 Peking 一进程 A（共享视图路径）；每进程八轮，上限 180 秒。代码修改后同模式复测；不与编译重叠。

使用已有 `transactional_lod` 夹具覆盖单线程/真实四线程、H/回收、局部样本 oracle、Pending、视图和失败；增加反复换相机/局部更新后再换视图/准备中异常后恢复，确认缓冲复用和旧视图不污染。样本全量 oracle 和原二进制八帧的批次身份/实际 mesh 对照；计时字段及新增工作计数不作语义一致字段。A 的基线不人为减弱。

性能报告比较 SetView、初始化、完整帧及静止/换视图子集；首次备用分配仍属首个 SetView。新布局持久约 `geometry Q + 2×projection Q`，不能只报告临时分配为零。没有全 Q 求值削减的主张。回归按规范 §7.3，至多一次定向复测；必要时撤回对应结构候选，不能掩盖退化。

## 实施结果

已完成。见[结果](../../research/cpu_refinement/gwr_02_view_storage_results.md)与[审查](../../reviews/cpu_refinement/gwr_02_view_storage_review.md)。相关夹具及 24 个自然逐帧结果一致；Peking C4 28.789→16.194ms，test129 C4 18.915→17.627ms。未改评分、排序或认证，进入 GWR-03。
