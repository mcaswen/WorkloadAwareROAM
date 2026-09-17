# QPC-04G 提案可行性诊断代码事实

2026-09-17。范围为本轮新增实验组件、对应脚本及入口，不覆盖整个Transactional生产模块。依据[规划](../../plans/cpu_refinement/qpc_04g_proposal_feasibility_plan.md)、[结果](../../research/cpu_refinement/qpc_04g_proposal_feasibility_results.md)和实际源码。以下未标注部分均为FACT；数学证明层级见结果及推导。

## 1. 文件、边界与调用方向

| 文件 | 实际职责 | 所有权/依赖 |
|---|---|---|
| `src/experiment/greedy_transactional_lod/TransactionalProposalFeasibilityAudit.h/.cpp` | 请求验证、局部输入捕获、旧路径对照、外部高度质量复核 | RecoveryTrace持有一个对象；读取State/Samples，独占私有Proposal及WorkLedger |
| `src/benchmark/experiment/TransactionalRecoveryTrace.cpp` | 解析可选`--proposal-feasibility SPEC.json`；Plan后、Apply前调用Observe | 没开开关时不构造对象；仍Apply原batch |
| `scripts/transactional_proposal_feasibility.py` | 一维必要/充分域、单点无进展证明、有限试值、双域独立核查 | Python纯计算，不调用生产控制器、不发起Apply |
| `scripts/run_transactional_proposal_feasibility.py` | freeze/capture/analyze/verify/report编排，配额、输入身份、轨迹比较、图 | 复用`run_transactional_platform.native_run`，原生Windows进程由Ubuntu编排 |
| `tests/TransactionalProposalFeasibilityAuditTests.cpp` | 实际目录、闭支持、只读状态、请求/绑定和见证复核夹具 | 临时目录自有；失败保留证据 |
| `tests/test_transactional_proposal_feasibility.py` | 有理内/外域、投影、进展与float反例 | 只跑定向unittest |
| `tests/CMakeLists.txt` | 实验probe和新增测试登记源文件、固定Boost头路径与严格FP选项 | 新组件不加入生产core、adapter或renderer |

数据方向：`production immutable snapshot → experimental JSON → independent Fraction model → explicit witness JSON → private native certification`。不存在算法层反向依赖实验模块、Python或研究文件。未新增后台线程、生产缓存、队列或公共策略枚举。

## 2. 原生类型与生命周期

`TransactionalProposalFeasibilityAudit`没有基类。构造函数一次读取SPEC：`protocol=qpc04g-v1`，最多2个机会、7个逻辑根，frame必须在轨迹范围内，同一frame和root不能重复。`_roots`保留每个请求中的根顺序；`_trackedRoots`用于原轨迹紧凑存续统计。

`Witness`保存Frame/Ordinal/Root、Height和完整Binding。可选witnessFile还指定原捕获sourceFile；`_expectedSource`保存其完整内容。构造时限制每目录项一个见证、ordinal<8、有限高度和已请求的frame/root；实际目录没有生成该ordinal时Observe也拒绝。

`_sampleRecords`为当前进程累计闭样本记录，不是独立Q数量。单项上限200k、单进程累计上限2M；编排端另计三进程总量。本次全轮55,745，未触及两层限制。

对象在RecoveryTrace返回时销毁。生产状态以const引用传入；每次Fit和PointwiseQuality都作用于独立Proposal副本和账本。不存在持久反事实状态或重放分支。

### 关键控制流

```text
构造：校验请求 → 可选见证与源文件
Observe：校验固定旧高/逐点政策 → 首次完整源绑定
  → 活动面身份扫描 → 根死亡/局部几何与view指纹
  → 指定机会：ReceiverCursor按实际顺序列出全部项
      → CaptureProposal：完整绑定 + 闭支持 + 配额
          → 私有旧Fit或B固定认证 → 如成功则逐点认证
          → 导出输入、缓存对照、旧理由/区间及费用
          → VerifyWitnesses：完整绑定/结构/范围 → 新逐点认证 → 旧最大值对照
  → 写只读费用
调用者继续Apply原Plan批次
```

`CaptureProposal`与`VerifyWitnesses`为私有职责方法，Observe不再混入逐项JSON和认证细节。`Binding`保存frame/version/Q分母、root/ordinal/kind/newVertex、完整配置、旧/新几何，使用17位double往返精度。大源文件独立逐字节比较；FNV64仅作重复线索，不参与认证。

`Closed`对所有Support的FaceSamples求集合并，包含不可见点与共享边。捕获保存样本整数编码/坐标和缓存值；离线可行性从几何和U16源重新算，不以缓存误差作接受依据。B虽有Free字段仍禁止外部改高；所有见证先复核原shape。构造提前失败且没有新点时输出`fitHeight=null`。

错误请求、过期输入、源变化、目录之外见证、输出覆盖、I/O错误均中止该次诊断；配额截断明确输出censored。WorkLedger保护旧认证及见证认证，每段20s/2M访问。普通路径未安装此组件时只有入口空指针分支。

## 3. 离线模型与证据

`Interval`保存有理上下界、开闭性、收紧它们的约束与零系数反证。`add`处理`c*z≤r`；正分母使用严格条件。`sqrt_enclosure`先识别有理完整平方，再以96位dyadic包络控制方向。

`affine_height`独立从实际提案几何求a/b；所有覆盖同一样本的面必须一致。`shape_failure`复算生产的UV有理平方最小角条件，不使用高度。`model_sample`恢复精确U16双线性参考、old曲面、view可见性、K和旧误差；可见性与缓存不符时标未知。

`constrain`维护三个独立域：新安全充分内域、新安全必要外域、旧统一最大值必要外域。最后一项不等于旧保守Fit模型；实际旧Fit成功才有原生导出的拟合区间，失败时`available=false`。本次全部正例已经被旧精确门槛排除，没有把未导出的保守模型归因为主要原因。

`analyze`的出口顺序：censored → 固定shape/构造失败 → B单例双域 → 自由高度完整闭Q → 系数/约束 → 必要域空 → 必要单点且等于旧曲面 → 单点不能存储 → 固定旧超标样本保持旧值 → 预定试值/双域验证 → 未决。

`excess_is_unchanged`要求旧超标样本同时满足b=0和a=h_old。只证明b=0并不足够，因为新连接可能已改变常数项。必要单点的无进展结论要求完整Q上`a+b*z0=h_old`，不会把试值未命中当成无解。

`evaluate_height`复用04E的独立Fraction几何/reference、04F的public_side和condition：binary64存值后，核心和实际float世界曲面分别重新闭Q枚举/定位/评价。进展使用128bit定向和。每个正例保留旧统一门槛的独立布尔结果。

试值顺序固定为旧Fit值、源高投影、安全内域中点、层次内部点；最多32个，重复binary64去重。内域空则只能在外域内试值并接受独立验证，不能因外域非空直接准入。有限失败仍是unknown；本次单项最多3次。

## 4. 编排、结果与恢复约束

初始baseline源、二进制、输入和caps已在改动前冻结。capture三条原轨迹，verify仅复核有见证的Peking/Sierra各一次；每次与04F的完整离散行比较。Freeze拒绝混用同一输出的不同捕获程序身份。收尾重构的binary只运行专项夹具及普通路径对照，另由命令记录其SHA。

首轮`analysis/`保留8个unknown_progress，后续`analysis-closed-domain/`补单点证书并关闭它们，最终`analysis-final/`在收尾证明/复用审查后复核同样分类与见证；不是覆盖旧结果。`report`保存精简freeze、43项约束/试值、28个原生结果、重复指纹、命令、成本和图。大样本与日志不进入Git。报告检查原生见证数量与冻结请求数一致。

INFERENCE：根几何/view指纹相同提示可复用计算机会，但不证明owner/cache、目录身份或生产失败生命周期完全相同；当前未实现失败缓存。

## 5. 验证与未覆盖边界

8项Python解析测试及一个C++专项夹具覆盖本轮数值方向、只读和输入绑定风险。三条原轨迹、两条必要复核和普通前后路径没有改变离散结果。详见结果报告中的成本表与来源。

未覆盖：新高度真的进入批次、donor能否配对、并行组合、持续恢复时限、连续曲面数学最大值、未来视图保证和新Fit的实时成本。新组件只为实验服务，不能直接搬进生产帧路径。
