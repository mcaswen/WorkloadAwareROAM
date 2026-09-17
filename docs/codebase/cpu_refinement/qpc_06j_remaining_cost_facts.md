# QPC-06J 剩余成本与诊断边界事实

范围：06I之后的生产状态；06J只扩展诊断输出。规划见[小规划](../../plans/cpu_refinement/qpc_06j_remaining_cost_audit_plan.md)，测量见[结果](../../research/cpu_refinement/qpc_06j_remaining_cost_results.md)。

## 1. 入口、职责和依赖

- `src/benchmark/experiment/TransactionalRecoveryTrace.cpp::RunTransactionalRecoveryTrace`拥有离线见证追踪和CSV。输入是冻结解析配置、见证坐标文本、输出目录；见证文本不是种子网格。种子仍由`TransactionalSeedBuilder`按配置生成。
- 每机会工具设置视图、直接调用`TransactionalReservation::Plan`，应用结果并独立验证状态，再导出统计。其`TransactionalExecution.Diagnostics=false`保留成功即停，但可开启失败提示影子认证。因此不能直接把诊断任务时间当正常成本。
- 本次只导出`WorkLedger.DonorCertified / DonorTouched / LocationTests / SampleEvaluations`为`quality-work.csv`四个count键，没有新增数值计算或生产缓存。
- `scripts/run_qpc_remaining_profile.py`是两个冻结输入的有限驱动，复用`run_experiment.py`与已有perf/Tracy/恢复分析器。依赖方向为脚本→实验入口→算法；算法不依赖报告脚本。

## 2. 实际路径与生命周期

正常路径：`Pipeline::Update → IdlePlan::Find → [命中直接返回 / Plan → Apply → Remember]`。SetView真实变化、提交非空拓扑等会清理相关空批状态。恢复诊断直接Plan，不走Update的空批复用。因此相同静止状态可得到相同空结果，但做更多配对检查。

脚本先核对各捕获与06I原生96机会完整比较字段；恢复CSV较少，核对`frame/hash/faces/raw/receivers/need/feasible/exchanges/free`。配对差异另存，仅接受静止段、正常无事务、曲面及投影均与前帧相同的情况，否则失败。Canyon65～95帧正是该分支；移动/返回配对计数未分叉。

原始数据属于每次独立进程。派生JSON/CSV进入文档数据目录，二进制、源快照、完整捕获与日志在忽略目录。`--analyze-only`不执行子进程采集，只重新归约，原命令文件不覆盖。子进程失败、超时、结果不一致均中止；未建立通用后台服务。

## 3. 捐赠状态与认证事实

`TransactionalReservation.cpp`先按同一接收人口分配预算，再取共同捐赠池。当`batch.Need`非零时，提前并行构建整个池的`Proposal`并做逐点认证。`DonorCertified`在每个池项进入时计一次；配对第一次访问该项时计`DonorTouched`。足迹则由`optional<TransactionFootprint>`按需缓存，作用域仅本次Plan。

`execution.Diagnostics=false`时成功交换立即退出该根的配对循环；不成功则继续完整池。故“全部触达”可由失败请求造成，不要求诊断穷举成功后的多余方案。移动/返回Canyon提前认证数等于触达数，不能预支惰性认证的减工收益。

`TransactionalProposals::Donor → TransactionalCertification::Measure → ErrorBounds`仍在逐点政策下执行。Measure不只写旧误差上下界；无法建立投影界时会拒绝。Reservation在逐点政策下跳过`Accepts`，不能自动推出Measure可删。当前代码没有“逐点接受蕴含旧Measure成功”的证明或分支替代。

## 4. 样本/视图事实

`TransactionalSamples::Prepare`拥有闭面归属、当前高度/投影和优先级修复，使用06I的Contains及持久ReferenceHeight；实际owner更新和Project仍按原顺序执行。`LocationTests`是位置测试计数，`SampleEvaluations`含视图Project，不能把后者全归局部维护。

`PrepareView`访问全部`_values`投影，再按活动面遍历`_faceSamples`归约、计算面优先级并`BuildOrders`。`PublishView`交换备用视图并移动优先级/索引，不再逐样本复制回写。`TransactionalPriorityIndex`块大小256，局部`PrepareRepair`只处理受影响块；有限前缀查询不是每次建完整平衡树。

`Pipeline::SetView`拒绝改变源比例、预算、政策等持久前提，相机不变时提前返回。本次没有改这些公共行为，也没有新增跨帧证书缓存。

## 5. 复杂度及采样解释

FACT：剩余逐点认证仍执行局部面覆盖和区间/有理表达式；样本数和精确数值位长不由局部点度界限制。INFERENCE：用s样本、f局部面描述时，覆盖循环包含O(sf)，不能仅按事务数计常数工作。

FACT：当前视图循环包含全Q、闭面贡献、面/点物理槽记录和捐赠关联扫描。INFERENCE：当前实现成本含O(q+c+N+Sf+Sv+a)及两侧分块排序，其中Sf/Sv为物理槽数、a为捐赠关联读取次数；不能将槽数一律换成活动面数。没有证明所有精确查询算法都必须全扫。

perf输出记录未知祖先，不能解释成完整调用图。Tracy任务时间和是任务区间之和，不是线程CPU时间；控制线程等待覆盖工作线程运行，不等于独立可删除的同步成本。

## 6. 变更风险、验证与索引

本次诊断字段没有改变账本所有权或并行归并。最终两个Linux构建与有限捕获/恢复诊断完成；各模式96结果与原生06I一致。复用06I相关持续状态、逐点和执行测试，不扩大测试矩阵。

符号索引：

- `RunTransactionalRecoveryTrace`：诊断配置、直接规划、应用、见证和工作量输出；依赖生产算法及独立验证器
- `TransactionalReservation::Plan`：冻结接收/共同池、提前捐赠认证、按顺序配对与预留；不拥有跨帧缓存
- `TransactionalCertification::Measure`：提案误差上下界及可投影性；不能仅以旧阈值已跳过判定其冗余
- `TransactionalPipeline::Update / SetView`：正常空批复用和视图生命周期；是诊断/普通物理工作差异的来源
- `TransactionalSamples::Prepare / PrepareView / PublishView`：局部续接和全域视图工作；持久样本事实与相机派生状态分离
- `TransactionalPriorityIndex::PrepareRepair`：按受影响256记录块修复索引

## Unresolved / Uncertain

旧Measure与新证书之间的定义域蕴含未证明；数值核最低成本和其他精确视图查询方式未知；根内缓存低复用不排除跨根复用；本次没有新的质量恢复或DOD同质量竞争结论。没有把这些未决问题补成“优化已充分”。
