# QPC-04F：显式目标与逐点质量证书的代码事实

日期：2026-09-17。范围：本次新增政策及直接调用链；默认仍为旧政策。基线为c2cc278工作区，实验二进制身份另见结果数据的sources.json。不声称重审整个事务算法。

源码主目录：[greedy_transactional_lod](../../../src/algorithms/greedy_transactional_lod/)；配置入口：[TransactionalLodSettings.h](../../../src/algorithms/TransactionalLodSettings.h)；诊断目录：[experiment/greedy_transactional_lod](../../../src/experiment/greedy_transactional_lod/)。表中未带路径的核心文件均位于主目录。

## 1. 模块、依赖和真实职责

| 标记 | 组件 | 当前职责 |
|---|---|---|
| FACT | TransactionalLodSettings / TransactionalTypes | QualityPolicy、QualityTargetPixels、QualityHeightRatio；Proposal持有只读证书共享所有权；WorkLedger增设最大有理位长 |
| FACT | TransactionalQualityEvaluation.h/.cpp | 从旧Certification提取区间、双线性源、插值、投影和精确误差；新增两表示域的覆盖/接口/可见性核对及定向整数求和 |
| FACT | TransactionalPointwiseQuality.h/.cpp | 提案级逐点屏幕/高度损伤、接收侧正进展、证书生命周期和批次共享贡献核对 |
| FACT | TransactionalSamples | ReceiverKey统一作用于初建、视图刷新、局部修复；BoxCandidates只枚举局部公开曲面包围框 |
| FACT | TransactionalReservation / FlipRecovery | 旧拟合成功后才执行新认证；失败继续旧目录；回收证书每快照一次，不在每个配对重新认证 |
| FACT | TransactionalPipeline | 发布前核对证书；SetView禁止变更目标、政策和几何规则 |
| FACT | TransactionalMesh / PublishedPosition | 输出与证书共用float位置转换；法线及渲染消费契约未改 |
| FACT | ExperimentCase / ExperimentReplay / catalog / schema / runner / suite | JSON验证、输入映射、任务身份和非事务算法字段过滤 |
| FACT | ExchangeQualityAudit / RecoveryTrace / QualityProvenance | 诊断政策身份、真实请求人口、工作账本、种子哈希和只读恢复解释；不能改变正常批次 |

依赖方向为实验入口→算法编排→提案/质量→已有状态、样本及数值事实。算法没有依赖实验代码或Python。数值模板留在模块内部头；只有数值单测额外显式引用固定Boost头目录，未把Boost变成整个算法公共依赖。

本次没有修改Proposals主体或ProposalEvidence：旧Fit沿用已有evidence，新的损伤检查各域定位一次、精确回退复用所选面。闭样本由私有证书拥有，不给旧可见样本对象增加第二种隐含语义。不存在全Q×提案缓存。

## 2. 配置、请求与种子

FACT：默认Legacy；新政策必须固定存活点高度、关闭旧HeightGuard、使用ErrorFirst。目标必须有限且为正。平台SeedBuilder和直接核心State两入口都验证。

FACT：新ReceiverKey只用可见样本缓存最大平方误差A，资格A>E*²，键(-A,stableID,slot)。原复合P与回收排序仍存在；旧SplitPixels继续决定DOD种子，未重解释成新质量目标。全部自然臂导出的初始网格哈希及面数相同。

FACT：任务哈希仅在新政策启用时追加版本标记及目标；旧默认身份保持。政策和目标参与持续设置相等判断，切换需要Reset。视图代次由SetView推进，不能把旧证书用于新相机。

INFERENCE：此规则解决“超标却因旧密度阈值而没有请求”的入口问题，不保证全部可见误差根都能进入有限前缀或生成可行提案。缓存double资格不是精确实数判定；正常Samples求值对参考/被测无效投影抛错，不把它记为达标；ReceiverKey防御性地不把非有限值加入普通全序。近阈值测试针对明确的相邻浮点目标。

## 3. 真实认证数据流

1. ReceiverCursor仍按旧目录产生提案，原CertifyReceiver执行形状/Fit/原局部最大值门槛。
2. 原认证成功且新政策启用时，PointwiseQuality::Certify检查完成拟合后的实际提案。一个旧成功、新失败项不会终止其余原目录。
3. E/B/F/H都经过该入口；R在原形状死端触发的原循环中检查新条件。损伤失败不会开启一个新R搜索规则。
4. Donor仍用原快速重剖分，旧成功后获得独立逐点证书；新模式不再调用配对相关的旧Accepts阈值。旧模式完全保留。
5. 命名空额、+1/+2/0接收成本、−2回收、无回流、回收唯一性及读写冲突保持既有机制。
6. Apply在Commit::Prepare之前ValidateBatch。几何或版本不一致、共享改变贡献都会抛错，不能先发布一部分再补救。

完整闭支持包含不可见样本。每个样本的高度平方误差u和当前参考可见屏幕平方误差a满足：

- u_new≤max(u_old,H*²)；
- 可见点a_new≤max(a_old,E*²)；
- 接收侧在两个表示域分别有严格正的超标平方误差和下降；
- 回收侧无需单独正进展，但逐点条件推出非增。

因此安全条件强于“只要求整个交换有正进展”。根最大误差下降不是本函数的承诺。

## 4. 两种曲面与数值对应

FACT：核心Point使用binary64；PublishedPosition使用float世界x/y/z，正是Mesh::Build发布的位置。两者分别认证，不将内部通过自动等同于公开通过。

核心域复用旧闭面关联；公开域由旧补丁float包围框局部枚举，外扩一格后精确包含。参考坐标在认证中直接精确映射为世界x/z，不用舍入后的逆变换代替。SameInterface检查正面积、面积和、身份边及新边落在旧接口；B只对实际外边界允许源高更新。

INFERENCE及前提：SameInterface不是一般自交三角化验证器；它依赖原E/B/F/H/R/D构造已经合法及旧状态共形。面积相同本身不构成合法性证明。所有存活点几何固定、正常目录不搬移接口，是补丁外不变性的必要条件。

区间先做充分接受/拒绝；相等、重叠或投影定义域不确定进入cpp_rational。参考可见性须与精确输入约定一致，不能由修改后曲面隐去困难点。进展按2^128定向整数区间求和；下界>0才接受，上界≤0拒绝无进展，跨零为未知。

FACT：最大位长只覆盖参与损伤/进展比较的选定有理量，不包含所有几何谓词中间量。普通Reasons在配对拒绝时可重复累加，不能据此推算独立失败提案总数。

## 5. 所有权、并发和失败

证书拥有State实例指针、Version、配置副本、完整有限提案几何与两个域的闭样本列表，附着在Proposal的shared_ptr<const ...>。拟合后认证，任何拒绝先清旧证书；局部任务各写自己的记录，现有执行器在屏障后合并账本。

ValidateBatch逐字段比较实际几何，不依赖哈希碰撞假设。对同域重复样本，只允许各提案在该点的旧新高度严格相同。阶段工作内存随批次释放，没有跨快照证书缓存。空批无需新证书数组。

诊断Recovery曾仍调用旧Accepts且Rank先过旧阈值，造成Canyon“未解释的可用交换”误报；已改为复用当前政策。修正仅影响只读解释，三条正常轨迹未重跑或改结果；新追溯与正常网格/工作逐帧相同。

## 6. 成本与限制

记核心关联a_i、公开包围框候选c_i、完整支持s_i、面数d_i，新增保守成本为：

W_quality=O(Σ[d_i²+(a_i+c_i)log(2+a_i+c_i)+(s_i+c_i)d_i])+W_exact。

公开域候选可能远多于最终闭支持。还需计入接口几何检查、证书有限几何复制、批次S个样本的O(S log S)共享索引、共享项的再次精确求值及析构。原拟合、提案生成、预留、视图、续接成本都仍存在。

有理算术依赖数值位长，以上不是固定CPU时间界；d有界不使s、c有界。质量计时task_wall_sum是任务墙钟之和，不是线程CPU时间，不可与阶段墙钟相加。quality_evidence_bytes是累计近似负载字节，不是峰值存活内存，完整进程峰值另记。

UNCERTAIN：没有连续曲面最大误差保证、没有运行时Q之外的逐点归纳证明、没有有限帧恢复时限、没有目录在新可行域内的完备性保证。没有形式化整个C++或float光栅行为；Lean仅沿用04E抽象核心。
