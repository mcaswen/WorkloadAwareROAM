# GTP-04 执行与测量代码事实

2026-09-14。范围为 `src/experiment/greedy_transactional_lod/` 的本次执行边界及两个探针；未改部分继承 [GTP-02](gtp_02_persistent_state_facts.md) 与 [GTP-03](gtp_03_dynamic_trajectory_facts.md)。下文均为 FACT，推断另标

## 1. 文件、职责和依赖

- 新 `TransactionalExecution.h/.cpp`：值配置与同步 `Run`，只依赖 Types/标准库；不拥有线程池、拓扑或业务缓存
- Pipeline：拥有执行配置副本，构造时拒绝多线程无 Dispatch；向 Reservation、Samples::PrepareView、Commit::Prepare、Mesh::Prepare 传配置。DynamicReference 使用默认串行 Pipeline，其反馈控制没有改写
- Reservation：私有 receiver 结果按全局前缀索引存储；只读并行认证后按旧顺序收集。Need 非零才预计算共同 donor 池；随后 pair/height guard/预留仍串行
- Samples：只把 view 的投影和面评分分块；Prepare 的局部修复、BuildOrders 和 PublishView 仍串行
- Commit：独占面记录按预分配槽与确定身份填写；incident/edge、活动尾交换、live 发布仍串行
- Mesh：私有输出块并行构造；Pending 合并/排序、容量增长、实际输出写入仍串行
- Probe：持有池适配器并注入回调；A 使用独立 DynamicReference，B/C 使用 Pipeline，不重复创建第二套当前状态
- 新 FamilyProbe：只调用公共 Classic/DOD BuildRenderData；独立导入最终公共几何作离线 Q 评价，不写回生产
- CMake：核心增加 Execution；测试/探针复用 MPR 的 MaterializationExecutor.cpp 和原 DOD ThreadPool.cpp。家族探针链接生产实现但生产没有反向链接原型；默认选项仍关闭

核心先有同步执行接口，探针再 Wrap 现有线程池，未把 MPR 状态/算法引入一般网格核心。Input/Validation 仍只链接实验入口。公共输出仍是已有 TerrainMeshData，不依赖上传后端

## 2. 执行配置与账本

`TransactionalExecution` 字段：Workers 默认 1，Diagnostics 默认 false，Dispatch 同步回调。Run 接收 phase、count、调用方账本及区间任务。chunks=min(count,Workers)，商余划分为非空连续区间；零项不派发。Workers=1 在当前线程调用，其他通过外部回调

每块独占 WorkLedger 和可选 thread::id 槽。Deadline/VisitLimit 从外部复制；任务内不访问共享计数。Merge 显式列出逻辑计数字段，Reasons 相加，局部 Seconds 带 `task_wall_sum_` 前缀。阶段 Seconds 使用外部同步墙钟，不是局部时间总和。所有块完成并归并后重新 CheckLimit，不能以每块单独未超限证明全批有效

WorkLedger::Execution 映射 phase 到 [处理项累计, 分块累计, 单次实际线程数最大值]；只在 Diagnostics 开启。自然结果不会保存每个线程 ID，只报告去重参与数量。任务分派不保证每块由不同线程处理，也不保证实际同步占满核心

Run 自己不实现排空，依赖明确的 Dispatch 契约。当前适配器把任务异常捕获到独占 exception_ptr 槽，ParallelFor 返回后再抛出；enqueue 异常会 Shutdown 排空并使适配器不可继续。普通任务异常不等于永久关闭池。实验仅单调用者，不支持外部一边更新一边读写状态

## 3. 数据流与确定性

`Update → receiver_stage → 有序收集 → donor_stage → 串行 pair/reservation → Apply`。receiver 目录内部尝试顺序不变，旧高优先级资源不能被先完成的低优先级任务抢占。原统计要求遍历整个 donor 池，所以提前认证没有新增中心人口

`Apply → Commit::Prepare → Samples::Prepare → Mesh::Prepare → CheckLimit → 核心/派生/输出发布`。面身份仍由规范化连接排序决定，填写身份为 oldNextFace-index；线程完成顺序不进入逻辑身份。顶点、边 map 的共享修复未并行。Mesh 从 PreparedTopology 的最终实际高度读取，失败发生在 live 发布之前

`SetView → 私有 Projection → barrier → 私有 Priority → barrier → BuildOrders → 发布 view/代际/样本 → 更新 mesh 代际`。每个样本只写一个 pair<double,bool>，没有 vector<bool> 打包写冲突；每个面仅一个线程按原闭样本顺序归约。owner、参考高度、mesh 高度和拓扑不在换视图时重算

INFERENCE：同一输入无异常时，并行改变调度而不改变逻辑值和所访问样本集合。依据是只读旧状态、独占索引结果、固定归约及有序消费；自然/解析对照提供运行证据，不是任意输入的机器形式化证明

## 4. 生命周期、失败与计时

池先于 Pipeline 创建，晚于 Pipeline 销毁，回调不逃逸轨迹生命周期。初始化单列 executor/state/sample/mesh；正常帧包含所有派发、等待、预留、维护和 mesh。任务局部记录/函数对象析构可能落在子阶段计时之外，但完整 frame 窗口仍包含，不能把各子项和当成穷尽完整时间

任务超时、配额或异常后不调用 live 发布；容量 reserve 可能已改变容量但不改变逻辑状态。解析检查直接在面填写排空后抛出，验证代际、样本、输出和 Pending 未变化。所有潜在分配/认证失败点仍在共同发布前

探针用 ownsOutput 记录目录所有权；拒绝已存在目录时不再往旧目录写 failure.txt。仅当前新建目录保留运行失败证据。成功路径无变化，最后这一错误路径修正未重新跑自然计时

## 5. 家族对照真实口径

FamilyProbe 使用 Formal 清单/相机，但覆盖清单为了旧配对而强制的串行配置：PassPolicy={}、EnableParallelSplit=true，三类诊断关闭；地形尺度、阈值、深度和预算保持。0～13 是自有历史初建，后续固定八视图，实际线程统计逐行输出

Import 用 public mesh 的 TexCoord 与 Position.y 作为实际曲面，按参数点合并稳定临时身份，发现同一点不同高度则拒绝；规范绕序后构造独立 State/Samples 做离线最终质量。它不是新算法执行时的全状态转换，也不进入计时

核心事件成本与旧家族不同，默认线程数也不同（此次 DOD 实际 8）。家族不是相同任务 reference，禁止把 C/Legacy 时间比当多核 speedup

## 6. 符号与风险索引

- Execution::Run / 内部 Merge：调用方独占输出、不嵌套复用共享 ledger、未来新增计数须同步登记
- Pipeline 构造 / Update / Apply / SetView：执行配置与所有发布边界
- Reservation::Plan：prefix/optional proposals/cache 的索引归属与 ordered reservation
- Samples::PrepareView：Projection / Priority 分块，BuildOrders 串行
- Commit::Prepare：faces 独占写入与 nextFace 确定规则
- Mesh::Prepare：Vertices 定长输出块与后续 Pending 合并
- Tests::ParallelExecutionAndFailure：真实线程、排空、异常原子性与续接
- Probe main / WriteReport：B/C 配置、execution 证据、分阶段墙钟和文件生命周期
- FamilyProbe Import / main：公共几何导入、默认策略、家族轨迹和计时外质量

UNCERTAIN：没有 TSAN/硬件 CPU cycles 或 allocator 归因。初次 test 单线程 +12.07% 及认证内部差值在排除明显 WSL 启动干扰的同会话核查中均未复现，现记录为无稳定版本相关证据，不能归因到某个源码变化。没有真实平台 GPU 上传/绘制、长轨迹恢复或多机器证据。当前数据不支持把这些缺口写成已解决
