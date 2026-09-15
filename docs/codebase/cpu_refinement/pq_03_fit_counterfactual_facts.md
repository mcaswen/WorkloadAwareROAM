# PQ-03：单高度反事实诊断代码事实

2026-09-15。范围是[小规划](../../plans/cpu_refinement/pq_03_fit_counterfactual_plan.md)引入的区间观察、独立一维诊断、测试和脚本；原持续追溯见[PQ-02事实](pq_02_residual_provenance_facts.md)。本页不把诊断能力扩张为生产拟合政策。

## 1. 文件、依赖及职责

| 归属 | 真实文件/符号 | 职责 |
|---|---|---|
| 核心 | `src/algorithms/greedy_transactional_lod/TransactionalCertification.h/.cpp` | 原Fit增加可选定长区间输出；原数值运算、选值与认证保持原样 |
| 实验 | `src/experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h/.cpp` | 独立模型求交/搜索、冻结提案私有反事实及局部文件输出 |
| 探针 | `tests/TransactionalQualityProvenanceProbe.cpp` | 解析`--fit-audit`，在frame7已规划、未应用时调用诊断 |
| 夹具 | `tests/TransactionalFitCounterfactualTests.cpp` | 闭式区间问题、失败分类、真实观察开关一致性 |
| 构建 | `tests/CMakeLists.txt` | 新诊断只链接追溯/夹具目标，注册`transactional_fit_counterfactual` |
| 脚本 | `scripts/run_transactional_fit_audit.py` | 冻结旧版本、限额采集、身份对比、局部归约与可选绘图 |

**FACT：** 依赖方向为探针→实验诊断→既有核心。核心不依赖实验层、输出路径或搜索器。诊断没有进入平台算法、GUI、预留、提交、线程调度或渲染构建路径。

实验命名空间为`ParallelRoam::Experiment::GreedyTransactionalLod`。新增诊断源文件使用MSVC`/fp:strict`，其他编译器使用`-fno-fast-math;-ffp-contract=off`；没有顺手改变原追溯翻译单元的浮点设置。

## 2. 类型与状态

### 核心观察结果

**FACT：** `SingleHeightFitInterval`是核心头文件中的值类型，含`Available`、`InitialHeight`、`LowerDelta`、`UpperDelta`。`Fit(..., SingleHeightFitInterval* interval=nullptr)`入口在非空时清零；一维约束循环完成后、原选值之前写入记录。`Available`表示区间已形成，不代表后续`Measure/Accepts`成功。空指针时不构造诊断容器、不记录逐样本状态。

### 实验模型

- **FACT：** `FitSampleConstraint`记录样本身份，以及`Weight`、`Difference`、`Factor`、`ClipW`、`WDerivative`、`NearValue`、`NearDerivative`。这些量固定于同一旧状态、视图和未拟合提案，不跨帧更新。
- **FACT：** `FitQueryStatus`区分`Feasible`、`Empty`、`NumericUnknown`；输出名分别为`feasible`、`model_empty`、`numeric_unknown`。
- **FACT：** `FitIntervalQuery`保留目标、上下界和最后收紧上下界的样本/行号。无收紧来源时样本为`InvalidSlot`、行号为−1；它没有单独保存“导致零系数矛盾的行”。
- **FACT：** `FitIntervalSearch`保存全部查询、`Best`、`LowerTarget`、`HasEmptyLower`及字符串状态。`Best`是当前已知模型可行端，不自动是经过几何认证的最优高度。
- **FACT：** 内部`LocalSample`保存固定参数点、提案面索引/权重、参考/批前/原选值高度及可见性。它只在一次`Run`内存在；随后各变体从私有提案的当前点高度插值。

所有容器均由调用栈上的诊断对象/局部值拥有。借用生产`state/samples`为const；没有共享可变缓存、新线程或跨帧持久诊断状态。文件流由局部RAII管理。

## 3. 方法与执行路径

### `Query`

**FACT：** 输入固定约束数组、非负目标和初始区间，逐行求交。每样本调用`work.Touch()`，每检查一行递增`Constraints`。四行依次为双侧误差、正分母、近面限制，公式见[结果§3](../../research/cpu_refinement/pq_03_fit_counterfactual_results.md)。

非有限输入/运算、严重消去形成的非零除数、极近的交叉端点返回数值未知。零系数且右端为负、或超过舍入警戒带的交叉区间返回模型判空。该警戒带是诊断防误判措施，不是严格浮点证明。全部约束满足才标为模型可行。

### `Search`

**FACT：** 要求原查询可行。首先查询0，之后在已知判空低端和可行高端间二分。每次仍使用原增量区间；最多32查询，括区宽不超过0.001px时停止。状态包括`input_unknown`、`zero_feasible`、`model_bracketed`、`numeric_unknown`、`query_limit`。未知或耗尽不通过抬高限额继续搜索。

### `BuildModel`与`LocalEvidence`

**FACT：** `BuildModel`复用`TransactionalProposalEvidence::Get`定位与double权重，读取`TransactionalSamples::Geometry/Parameter/Clip`，按生产Fit相同次序独立重建四行所需系数。没有复制整份生产Fit或重新实现接受器。

**FACT：** `LocalEvidence`汇总原支持面全部`FaceSamples`并排序去重。这些是闭面贡献，不仅是唯一owner样本；不按可见性删除。它检查未拟合提案与旧曲面高度差不超过1e−10，以保护本E提案特有的旧曲面基准。这个double诊断检查不能证明未来翻边后的模型仍正确。

### `Run`

实际签名借用`state, samples, approved, future, returned, witness`并接受输出路径。规划中的通用`limits`参数没有引入，60秒和200万次样本访问在此限定入口内冻结。

~~~text
探针解析模式与冻结q2
→ 原24帧控制器在frame7生成原批次
→ Run借用首个exchange的Receiver
  → 核对B、HeightGuard关闭、E、根/新点身份、单自由点、7056样本、1724469目标
  → 局部复制approved，还原新点在旧根上的初高
  → 清理HeightProof/ErrorLower/ErrorUpper，原Fit观察重放
  → 核对初高、点/面、原目标和证书；重建原区间并逐值比较
  → 输出固定系数；有限阈值搜索
  → 三个私有选值分别Measure/Accepts，保存逐点证据
→ 返回探针，原batch照常Apply/ConsumeMesh
→ 余下原轨迹继续执行
~~~

**FACT：** 三个高度分别是原区间近零投影、双线性源高度投影和最好模型可行区间的中点。后者只保证确定性内部选择，不按未来视图或q2再次挑选。每次改高前清除私有旧证书，调用生产`Measure/Accepts(τ0)`；只有模型搜索选值额外查询向下取整的较低微像素阈值。其他两项输出`lowerTargetChecked=false, acceptedLower=null`。

**FACT：** 当前自然输入三项均一次认证成功，没有实现或触发相邻高度重试循环。规划允许最多两次相邻值重试是上限，不是强制工作。

**FACT：** `Reference`直接从原始高度样本做双线性插值。`Error`计算齐次投影位移，检查正分母/近面但不做完整视口可见判断；因此返回视图q2的值明确是离屏投影。局部可见超额使用已有`Projection(sid).Visible`筛选。

## 4. 失败、预算与输出生命周期

**FACT：** 探针在创建输出目录前拒绝非B、非冻结q2、未知参数；已有输出目录不覆盖。`Run`内部输入不符、证据不一致或限额异常由局部捕获，写出`input_or_model_mismatch`或`budget_exhausted`，然后原轨迹仍继续。`numeric_unknown/query_limit`也明确落盘。

所以**整个进程退出0不等于诊断成功**。结果必须检查`fit-counterfactual.json`的`status`、`searchStatus`和各候选实际认证。脚本保留负结果；仅`complete`状态生成图与局部分析。输出流初始化本身的I/O失败会传播到探针外层，不能靠诊断状态屏蔽。

每个变体还写入`fit-variants.jsonl`并刷新，以保留已完成候选。所有本轮文件放在独立运行目录；真实`frames.csv`、`witnesses.csv`、`transactions.jsonl`、`recovery.jsonl`和网格仍由旧追溯路径负责。

## 5. 脚本身份和复现

**FACT：** `freeze`要求当前旧程序SHA匹配PQ-02同模式命令，保存旧程序/四份改动前来源以及原见证来源。`collect`记录修改后程序与六份C++/构建来源，复用`native_run`的180秒/8GiB隐藏原生进程。原生命令记录已存在时复用，不覆盖运行。

`report`比较新默认及指定诊断目录与旧24帧`CORE_FIELDS`、三个逐字节诊断文件、五份实际网格SHA。`--audit-name fit-audit-schema`用于本次字段修正验证；初次输出保留。报告不重新运行全Q质量评价。

局部归约从已记录模型找零系数平台，从逐点CSV找相对原选值的退化见证。图只展示已认证点和查询点；没有拟合或额外求值曲线。`--figure`复用既有`asset_preview`字体与Matplotlib，绘图不进入C++程序。

## 6. 成本与复杂度事实

**FACT：** 本次诊断为0.150314s、128141样本访问、258530约束、28224筛查和1次精确回退检查；详见结果费用表。局部文件输出包含在内部墙钟，三个候选段不等价于纯`Measure`时间。`EvidenceBytes`为证据累计分配账本，不覆盖所有诊断容器或峰值RSS。

**INFERENCE：** 单次模型查询O(s)，搜索O(Ls)，空间O(s+L)。`TransactionalProposalEvidence::Get`含O(log s)身份查找，首次覆盖面查询含f个面；闭补丁去重还排序，因此当前准备实现含O(s log s+sf)，不能按规划的预期O(s)简写。实际原认证仍需计入精确数值位复杂度。没有全生产状态复制，但局部样本量仍可能大。

**FACT：** 新默认完整进程相对旧基线约+2.49%，未触发开发快验5%筛查门槛。这不构成稳定帧时间结论或零开销证明。

## 7. 验证及规划差异

解析夹具覆盖单/双目标闭式解、退化单点、源偏好区间外、近面限制、非有限/消去未知、访问限额；真实Fit夹具覆盖观察开关结果/工作一致、失败清空及二维不冒充一维。初次近面夹具导数写反已修正，未修改生产算法来迎合测试。

自然样本保持原24帧，三个模式与旧基线一致；新增CLI拒绝检查只针对两项新前提。未执行全CTest/后端矩阵、未来分叉或新采样。

与规划的具体差异：

1. `Run`直接接收已有三视图和见证，限额固化于独立诊断入口，没有泛化配置系统。
2. 实际预处理复用了带排序/二分查找的证据容器，费用和O(s log s)项如实保留。
3. 模型可行区间取中点；没有为最小化源距离或q2追加次级优化。
4. 字段修正导致一次额外、同输入的诊断验证，已在规划执行前补记；未当作额外统计样本或新搜索机会。

## 8. 符号和未决项

核心入口：`SingleHeightFitInterval`、`TransactionalCertification::Fit`。实验公共入口：`FitSampleConstraint`、`FitQueryStatus`、`FitIntervalQuery`、`FitIntervalSearch`、`TransactionalFitCounterfactual::{Query,Search,Run}`。内部数学/几何帮助函数：`BuildModel`、`LocalEvidence`、`Height`、`At`、`Reference`、`Error`；内部输出帮助函数：`Seconds`、`Name`、`File`、`Number`、`QueryRecord`。测试入口：`ClosedForm`、`Observation`；脚本入口：`freeze`、`collect`、`report`、`figure`。

### Unresolved / Uncertain

- **UNCERTAIN：** 采用任一反事实后完整24帧质量与决策会如何演化；本轮未分叉。
- **UNCERTAIN：** 一般新连接是否可以用相同仿射基准；当前只审计固定E。
- **UNCERTAIN：** 是否存在同时兼顾源高度、逐点代价和恢复速率的简单生产选值政策。
- **PLANNED：** PQ-04调查frame8有限局部形状恢复，不属于本轮实现。
- 已确认输入所有权和依赖方向；没有尚未追踪的跨线程诊断生命周期。
