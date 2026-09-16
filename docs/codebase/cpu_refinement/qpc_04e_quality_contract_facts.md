# QPC-04E 只读交换质量审计：源码事实

日期：2026-09-17。类型：本次局部实现事实；不替代生产算法全量架构。关联[规划](../../plans/cpu_refinement/qpc_04e_quality_contract_optimization_plan.md)、[结果](../../research/cpu_refinement/qpc_04e_quality_contract_results.md)。

## 1. 边界与依赖

`src/algorithms/greedy_transactional_lod`没有本轮修改。新能力属于`src/experiment/greedy_transactional_lod/TransactionalExchangeQualityAudit.h/.cpp`；CPU benchmark编排可调用它，生产core/adapter不引用实验层。CMake仅将采集器加入CPU探针和专项测试。

| 文件/接口 | 实际职责 | 不承担 |
|---|---|---|
| `TransactionalExchangeQualityAudit::Capture` | 借读当前State/Samples/CertifiedBatch，导出完整两侧几何、源定义、局部Q与全根人口 | 提案生成、认证、筛选、Apply、HeightProof缓存写入 |
| `TransactionalRecoveryTrace` | 解析互斥有限帧，Plan后调用Capture，随后原批次正常Apply | 新质量政策与新轨迹 |
| `transactional_exchange_quality.py` | 独立有理几何、完整Q对应、损伤/进展及组合核查 | 候选搜索、重新拟合、生产数值内核 |
| `run_transactional_exchange_quality.py` | 冻结输入/二进制，限定四轨迹及资源，逐帧比对历史 | 新场景生成、同质量性能竞争实验 |
| `analyze_transactional_exchange_quality.py` | 请求缺口、保留表、工作量、图与结构化汇总 | 改动运行时决策 |

选择对应规划：Reuse生产只读数据和现有原生进程runner；Extend原追溯入口及tests登记；Create有限采集器、独立数值分析和报告编排。没有把私有数值内核提取为公共API，也没有引入通用transaction/profiler框架。

## 2. C++所有权、状态和失败路径

Capture签名为三个const引用、frame、output和sampleLimit。函数不保存跨Apply引用；局部容器、流和CaptureWork均在返回时释放。`Proposal`虽然有mutable HeightProof，采集器从不调用可能填充它的生产Certification函数。

入口先核对`batch.Version==state.Version`；同名目标存在则拒绝覆盖。`exchange-source.json`在同一独占输出目录首帧写一次，保存原始U16和尺寸。主编排本身拒绝复用已有输出目录，因而后续帧不会借用另一资产的source文件。

内部路径：

- `ClosedSamples`遍历每个旧support面的`FaceSamples`，计association并用set去重。这里不是Proposal.Samples可见子集。
- `Side`从State的旧support取得完整points/faces，直接读取提案最终Points/Faces，导出每个样本的整数坐标、缓存可见性与旧缓存值。缓存值只供独立对应检查。
- `Roots`扫描活动面及其现有闭贡献，计算当前可见平方误差最大值、旧P资格和前缀标记。单独计`RootContributions/RootsSeconds`，不伪装成局部事务费用。
- `CaptureWork`另记源写入、association、局部record/visible数量及耗时；不引用生产WorkLedger。
- 超过sampleLimit时输出`censored-samples`及空exchanges，保留原approved分母；不把前半批当完整批次。正常轨迹继续按原batch发布。

当前CLI为`--recovery-trace RESOLVED WITNESSES OUTPUT --exchange-quality-frames I,J`，最多两个不重复合法帧。与原boundary/priority观察选项互斥，默认关闭。原有限witness诊断仍照常执行，其成本不归入生产更新计时。

## 3. 独立判断的数据流

`geometry`将实际double精确转换为Fraction；`source_height`用U16、样本有理坐标和实际HeightScale恢复双线性参考；`closed_population`局部枚举六组Q，必须与导出样本身份/整数坐标完全一致。

`height_at`逐面计算有理重心系数，检查正方向、覆盖和共享高度一致。`evaluate_side`还检查存活几何未改变；每点计算旧/新高度残差和透视屏幕平方误差。`screen_errors`以独立参考确定可见性；可见参考对应的被测近面失败返回unknown，不能赋零继续。与缓存可见性不一致也返回unknown。

`summarize`复用一次几何结果计算六组操作点。所有逐点比较均在Fraction域；进展用`sum_interval`的128位二进制定向区间。区间完全正才记positive，上界≤0记nonpositive，跨零记unknown；精确界另存字符串，浮点列用于呈现。`combine`要求共享记录完全一致且旧高=新高，才去重；否则不能进入批级接受。

自然批次的原资源/结构正确性来自现行Plan/Apply及原追溯校验；新检查不是一般多边形合法性或并发安全证明。组合未通过会撤下批级accepted标志，不将孤立证书相加冒充独立事务。

## 4. 形式模型

`docs/research/cpu_refinement/formal/quality_contract/QualityContract.lean`独立于旧闭包/物化工程。`Scalar`显式列出线性序及加减保序法律；`cap/excess/totalCost`是定义，15个定理是证明体。主要声明为`pointwise_iff_excess`、`sequence_envelope`、`potential_nonincrease`、`potential_zero_iff`、`common_maximum_bound`。

该模块没有实例化实际double、Rat、Real或C++容器；15条是参数化模型中的内核证明，不是15条生产正确性证明。脚本Fraction检查与Lean不共享执行代码。

## 5. 真实执行路径与费用

```text
旧输入 → SetView → 原Plan → （选定帧）Capture
                          └→ 原Apply → ConsumeMesh → 原追溯/哈希

Capture JSON/U16 → 独立闭支持枚举 → 有理旧/新曲面和投影
                → 六参数逐点检查 → 两侧/批间共享接口核查
                → 保留/未知/损伤原因 + 工作量 + 请求缺口表
```

主要费用不是O(1)：关联去重、局部bbox测试、逐sample逐面覆盖、精确投影及位复杂度都存在；全根扫描另外为O(active contributions)。证据跨参数复用避免重复定位，但不删除首次证据、未来缓存失效或生产续接费用。报告没有使用Python时间预测C++帧时间。

验证共71个原批准交换、7个观察批次，含2个空批；闭支持、共享接口与有理数对应均完成。312个自然机会与历史哈希/公共工作相同。默认CPU路径24帧及一次复测也相同；具体性能波动与有条件Gate结论见结果页。

## Unresolved / Uncertain

- 新质量请求如何替代原P资格、如何改变目录/失败缓存，尚未实现或证明。
- 全闭支持证据的高效生产数值过滤、失效策略和真实持续费用未知。
- 固定Q的样本保证不涵盖Q_eval、连续地形或float渲染输出；完整持续策略还没有运行。
- 原批准batch的子集不是新greedy决定；Canyon全拒不能推断重新配对/拟合必然失败。
- 单次快验和一次复测只能排查明显回归，不能提供正式同质量性能结论。
