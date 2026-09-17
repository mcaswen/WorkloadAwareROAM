# QPC-06E：提案面证据代码事实

2026-09-17。依据[小规划](../../plans/cpu_refinement/qpc_06e_face_evidence_plan.md)，仅覆盖新面组件、逐点认证及其直接数值/测试依赖。

## 所有权与依赖

FACT：`TransactionalQualityFaceEvidence.h/.cpp`拥有一个冻结`QualityFace`，含区间/有理两份惰性系数。系数存三点坐标和高度、三边差、单独CA差与原面积表达式值；不会重新采源高度。它不持有State、Samples、提案或线程池引用，不判断质量是否接受。

FACT：`TransactionalPointwiseQuality::Certify`在原域接口检查和候选整理后，为旧/新面建立两份私有数组。每个域结束即销毁，下一域重建；不会跨核心/float、提案或帧共享。数组完成建立后不增删，SampleEvidence返回的面指针仅在该循环内使用。新对象不进入持久QualityProof。

FACT：`TransactionalQualitySampleEvidence::Cover`的新重载沿用原三边闭包含、首面选择和歧义回退顺序。样本精确坐标仍由其原有惰性缓存提供。旧vector<QualityFace>入口、`CoverPrepared`和`Height`保留，供兼容调用和独立oracle使用；没有重写旧Fit/Measure。

## 数值与逻辑工作

FACT：区间沿用原`Interval`和严格浮点编译。CA按原减法计算，不从反向边取负；面积、w0/w1/w2、除法和最终高度加法树保持。精确系数只在第一次ExactSide/ExactHeight触达时准备。面空间在对象中预留，因此“惰性”省的是构造/算术，不能说未付对象空间。

FACT：每个对象固定大小在本轮Linux为1552字节，有理数动态limb内存另计。`quality_evidence_bytes`增加面数组容量×对象大小；这是累计逻辑容量估计，不是峰值RSS或完整堆分配量。原先证书字节口径也不是完整分配器账本。

FACT：新`QualityFaceEvidenceWork`记录对象数、区间/精确构造、边查询和高度查询数；在认证结束时归并到WorkLedger.Reasons，不改逻辑Touch/精确分支数、进展累计、目录或预留。Tracy开启时每次认证追加一个`gtp.face_evidence`短区间，其文本按顺序导出上述7项；正常构建没有事件或字符串拼接。

## 证据与限制

FACT：独立数值专项用未修改的原公式核对840组坐标/尺度、逐边精确值/区间端点、高度插值；零面积仅核对区间失败传播。两域和反序共享边检查首面相同，精确覆盖回退次数相同。惰性计数验证每面每数值域最多准备一次。另复用逐点政策与核心持续测试。

INFERENCE：输入点与表达式树相同，缓存子式保持原值；结合原样本/面/边顺序和拒绝分支，推出成功同任务认证及发布结果一致。专项和有限自然轨迹支持该推理，没有新增Lean或全输入机器证明。

INFERENCE：输入点与表达式树相同要求稳定舍入设置、有限有效几何和正常未超时路径；源代码没有改变这些前提。原始和准备入口重复一段闭覆盖循环是保持未优化oracle的明确边界，不是两套质量政策。

FACT：[阶段结果](../../research/cpu_refinement/qpc_06e_face_evidence_results.md)记录Windows P完整移动下降11.85%/10.91%、Linux下降7.72%/6.71%，保留候选。旧Fit调用数和主要费用仍在，没有更改质量结论。

## 未确认事项

UNCERTAIN：面少样本也少或早拒绝时，额外对象/所有边准备可能不划算。有理位长仍影响单次操作成本，面查询仍最坏O(ud)，视图、旧Fit、接口核对和持久续接不因本机制变便宜。本轮有限收益没有排除其他输入下的空间/早拒绝代价，也不证明性能极限。
