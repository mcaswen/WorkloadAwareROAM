# QPC-06A：逐点认证降本代码事实

2026-09-17。范围仅覆盖本阶段认证求值、调用者、测试和取证；不是全算法重审。生产目录为[src/algorithms/greedy_transactional_lod](../../../src/algorithms/greedy_transactional_lod/)。

## 模块、状态与依赖

FACT：`TransactionalQualitySampleEvidence.h/.cpp`拥有一次样本/表示域的六个optional值：区间/精确参考、坐标、参考Clip。借用State和Samples，sid和域在构造后不变。它不拥有Proposal、不判断质量接受、不跨帧，不保存覆盖面或被测高度。

FACT：`QualityEvidenceWork`是提案内物理构造/请求计数；样本循环内只做整数累计，QualityWork析构时汇入既有Reasons。它不影响Touch、CheckLimit和精确分支条件。精确对象在首次需求才构造。

FACT：依赖为PointwiseQuality→QualitySampleEvidence→QualityEvaluation→State/Samples；QualityEvaluation没有反向引用证据对象。`CoverPrepared`接收坐标和惰性精确坐标函数，原Cover仍作兼容入口；面/边顺序不变。`VisibleBounds`三态判定与`ExactVisible`被新旧入口共用。`ScreenPrepared`复用参考Clip，但每个不同高度仍独立投影。

FACT：CheckSample保留可见性→高度/屏幕损伤→正进展的原顺序；支持候选枚举、双域、拒绝理由、证书Owner/Version/Config/几何绑定和ValidateBatch共享贡献核查保持。旧Fit/Measure仍使用原入口，无跨语义证据混用。

## 数值与异常

FACT：`AddBinaryBounds`只替换快速区间分支的两个有限端点，有理精确分支仍用`AddBounds`。按IEEE位恢复准确整数有效数和二次幂，右移余数用于负floor修正；超字宽右移显式商零。非有限端点进入原有理转换异常路径。

FACT：核心继续以`/fp:strict`编译；未改变公共质量配置、默认政策、固定旧点或算法选择。CheckLimit访问点没有删除，真实时间超限仍可能随执行变快而改变完成与否，不能把被删失样本归为普通拒绝。

## 调用路径与取证

正常：Reservation→旧CertifyReceiver/Fit→PointwiseQuality::Certify→每样本证据→原证书→ValidateBatch→Commit。新数值缓存只存在于第二层认证。

诊断：`TransactionalProposalFeasibilityAudit`可选`costAudit`在冻结高度见证上预热一次、重复三次Certify并释放证书；不把这些临时Proposal送入Apply。保存秒数、理由、旧规则是否接受和访问量。原目录43项包括未进入新认证的失败，不伪造全部具有认证时间。

`run_transactional_quality_cost_reduction.py`复用原runner与04F配置，保存程序身份，普通平台与提案取证分开。`analyze_transactional_quality_cost.py --reduction-input ... --output ...`使用独立目录归约；未指定新参数时仍处理04F历史。

## 费用、边界与核查

INFERENCE：每域每样本每种表示的参考和参考投影构造至多一次，但覆盖仍随样本数乘局部面数增长，精确位长、旧Fit、接口和批次检查未消除。对象峰值按同时处理的线程数增长，没有全Q缓存。

专项测试使用独立cpp_rational转换/除法核对所有有限指数、正负及三种尾数，共12,282值；覆盖±0、次正规、最大有限及非有限拒绝。解析状态逐样本检查旧新参考、坐标、覆盖与人口，并保留原损伤、双域、证书篡改、过期、共享贡献和续接用例。

UNCERTAIN：有限核查不是C++形式验证；质量恢复、连续地形上界和当前新政策竞争性仍未解决。性能出口见本阶段结果，不能从删掉重复调用直接推定完整收益。
