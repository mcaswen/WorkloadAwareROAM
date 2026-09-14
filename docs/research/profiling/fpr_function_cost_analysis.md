# 函数热点对应的算法、数据结构与复杂度

2026-09-14；本次只补分析，不改变算法。返回[总报告](cpu_function_profiling_findings.md)。数值明细见 [test129 全函数表](fpr_test129_function_details.md)、[Peking 全函数表](fpr_peking_function_details.md)、[时序](fpr_thread_timeline_details.md)、[逻辑账本](fpr_workload_details.md)。下文的 C00～C13 是明细表的算法索引。

## 证据和复杂度口径

源码按 `8ef2882` 当前实现核查；FPR-02/03 记录的程序及逐文件哈希继续作为采样身份。FPR 后续增加的是观测和家族接入，没有实施 GWR。源码链接用于定位实际机制，不能拿当前行号替换旧二进制的指令映射。

- **测得：** perf 栈顶/祖先事件权重，Tracy 区间墙钟，原 `WorkLedger` 的逻辑计数。
- **源码推导：** 循环、容器和依赖带来的工作/空间上界。不是实测时间拟合，也不是已经形式化的 CPU 时间定理。
- **未测得：** 每个未标记函数的精确调用次数、所有分配次数、缓存未命中、内存带宽、精确算术的中间位数、真正的语义 span。不能从热点符号补出这些数字。

| 参数 | 定义 |
| --- | --- |
| n、v、e | 当前活动面、活动顶点、边记录数量 |
| nₛ、vₛ | 已使用的面/顶点槽数组长度，包含可复用空槽；不等同于活动数量或 capacity |
| q | 公共样本数量：test129=98,817；Peking=1,790,881 |
| a | 所有闭面—样本关联数；共享边样本可重复关联，通常大于 q |
| r、m、c | 被消费的请求前缀、回收池、每 root 目录上限；本次 r,m≤64，c≤8 |
| b | 成功事务数，包括用空额度执行的细化；不能用 r 或线程数代替 |
| d | 当前最大 incident-face 数；对通过当前形状不变量的内部点，最小角 atan(1/3) 给出 d≤19，不是 30°/12 的示例值 |
| fᵢ、sᵢ、aᵢ | 提案 i 的新面数、去重可见样本数、收集前的闭面样本关联数 |
| z | 新面包围盒内实际测试的位置数；包含框内但面外的失败测试 |
| t、sΔ、aΔ | 局部状态/索引修复范围、待修样本数、受影响面样本访问次数 |
| u | 尚未被消费的输出脏块数量 |
| ℓ、χ(ℓ) | 精确有理数中间整数位数及相应算术成本；本轮未测 ℓ |
| p | 实际允许的线程数，本次 GTP 为 4 |

有序 `map/set` 的查找、插入、按 key 删除按 O(log 集合大小) 计；数组访问按 O(1) 计。精确谓词另计 χ(ℓ)。后文将 `1+log x` 简写为 `log⁺x`，避免空集合/单元素时符号失真。拓扑支持有界不代表样本数有界，一个很大的面仍可覆盖大量 Q。

<a id="c00"></a>

## C00：完整帧与控制边界

入口见 [TransactionalPipeline.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalPipeline.cpp)。实际次序如下，箭头表示控制先后，框内各派发同步返回后才继续。

```text
ProfileSession 的帧窗口
  SetView
    相机/分辨率未变：直接返回
    否则：PrepareView → 更新视图代际 → PublishView
  Update
    Plan
      Prefix → 接收目录/认证派发 → 按原顺序收集成功项
      命名空额度 → DonorPool → 有需要才做回收认证派发
      全局顺序配对/冲突/预留
    Apply
      空批次：直接返回
      Commit::Prepare → Samples::Prepare → Mesh::Prepare
      Commit::Publish → Samples::Publish → Mesh::Publish
帧窗口外：Consume、文件输出和独立质量评价
```

`Initialize` 在这些入口会被调用，但正常八轮中已经完成初始化，调用只是早退。不能把 `gtp.initialize` 的区间数量当成重建 Q 的次数。`Apply` 的空批次不会修 mesh，但 `Plan` 仍生成前缀提案；Peking round2/3/5/7 就展示了“无修改仍有发现成本”。

因此完整工作应写成：

\[
W_{frame}=1_{view\ changed}W_{view}+W_{receiver}+1_{need>0}W_{donor}
+W_{reservation}+1_{b>0}(W_{prepare}+W_{repair}+W_{publish}).
\]

各并行阶段同步分隔，不能把全帧工作一次除以 p。更不能仅用最后的几何写入时间命名完整拓扑算法耗时。

<a id="c01"></a>

## C01：视图刷新、投影与面评分

源码：[TransactionalSamples.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalSamples.cpp)，`PrepareView`、`Project`、`Priority`、`PublishView`。

| 函数/子路径 | 实际工作与 early-out | 工作 / 暂存 |
| --- | --- | --- |
| `Decode`、`Parameter` | 在固定 6 个组中恢复样本整数身份，再转参数坐标 | O(1)；常数次整数除余，不是搜索整个样本集合 |
| `SourceHeight` | 读取四个原始高度并双线性插值 | O(1)；本次换视图复用旧 reference height，不对全部 q 重采原资产 |
| `Clip` | 固定 4×4 变换 | O(1) 双精度算术 |
| `Project` | 先投影参考点判断可见性，可见才投影被测高度并算屏幕平方误差 | 每样本 O(1)，但每次换视图遍历全部 q；参考不可见仍承担读取和参考投影 |
| `PrepareView` 的暂存 | `Projection.resize(q)`、`Priority.resize(nₛ)`，先创建/初始化目标数组 | Θ(q+nₛ) 时间/空间，目前串行；不是四线程投影的一部分 |
| `view_projection` lambda#1 | q 个独立样本，读取旧状态，写唯一暂存项 | Θ(q) 工作，理想分块计算 O(q/p)，另计派发/同步 |
| `view_scores` lambda#2 | 各面依次归约自己的完整闭样本关联，再算三个顶点的屏幕边长 | O(a+n log⁺v)；`Vertex` 用身份树查询，不是裸数组。按面分块，不按样本数均衡 |
| `Priority` | 三条投影边长与当前面误差最大值取 max | O(1) 算术；高紧迫性不保证接收认证有样本证据 |
| `PublishView` | 全 q 写回误差/可见性，替换 priority 与三份有序结构，并销毁旧结构 | O(q+nₛ+n+v) 的线性工作项；实际有序树析构亦在此区间 |

换视图至少存在三遍不同的工作：暂存初始化 q、投影 q、发布 q，此外还有 a 个面样本关联归约，不能只记 `Project(q)`。Peking 五次视图变化的 `SampleEvaluations` 中，**8,954,405=5q** 正好来自全量投影；八轮总数 8,955,073，余下 668 来自局部修复。

Tracy 不重叠账本中，Peking 五次投影合计 26.254ms、面评分 28.317ms、视图暂存准备剩余 33.011ms、视图发布 40.527ms，另有建序 65.364ms。这不是一个可以仅靠优化投影循环消掉的成本。

`PrepareView` 的 33.011ms 是扣去两个派发和 `BuildOrders` 后的实际剩余。源码显示数组创建/初始化处于这里，但没有进一步的分配/缺页/内存带宽证据，**不能把全部 33.011ms 宣称为 malloc 或某种缓存原因**。perf 中 lambda#2 自身 16.91%、lambda#1 自身 7.85%，是被内联到回调体中的计算/访问，不是 `std::function` 转发开销。

空间方面，原样本值含 owner、reference/mesh height，换视图暂存只存新的投影结果及 priority/索引；没有复制四份完整状态。但持久 q 与临时 q 的读写仍然真实存在。

<a id="c02"></a>

## C02：全域建序与拓扑资格查询

源码：[Samples::BuildOrders](../../../src/experiment/greedy_transactional_lod/TransactionalSamples.cpp) 和 [State::Vertex/Face/IsBoundary](../../../src/experiment/greedy_transactional_lod/TransactionalState.cpp)。

| 函数 | 数据结构和循环 | 复杂度 |
| --- | --- | --- |
| `Vertex(id)` | `_vertexIndex.at(id)` 查物理槽，再 `_vertices.at(slot)` | O(log⁺v)，含边界检查；不是 O(1) 稳定身份寻址 |
| `Face(slot)` | 数组读取并拒绝不活动槽 | O(1) |
| `IsBoundary(id)` | 枚举点的 incident faces，逐面查与中心相关的边，遇单面边早退 | O(log⁺v+dᵢ log⁺e)，内部点通常走完整邻域 |
| `BuildOrders` 面部分 | 全 n 个活动面检查 priority，将符合阈值者插入 `(−P,ID,slot)` set | O(n+n_c log⁺n_c)，n_c≤n |
| `BuildOrders` 回收部分 | 扫 vₛ 个槽；每个活动非边界点求邻面 priority 最大值，插入 donor set 和 cost map | O(vₛ+Σᵢ(dᵢ log⁺e+log⁺v)+v log⁺v) |
| `clear` / 旧结构释放 | 遍历并释放当前树节点；换视图目标树初始为空，旧树主要在 PublishView 替换时释放 | 各自按实际节点数线性计费，不重复放进两处 |

一个安全的总界是：

\[
W_{orders}=O(n\log^+n+v_s+v\log^+v+\sum_{i\in active}d_i\log^+e).
\]

Peking `IsBoundary` 自身 13.44%，`BuildOrders` 含调用 15.55%。前者也被接收目录调用；**不能直接相减为“建序中剩余函数=2.11%”**，必须用带上下文路径统计。全调用边明细保留了来源。Tracy 的建序 65.364ms/5 次是完整墙钟证据。

将全序树改为 top-r，只可能去掉相应排序/节点维护；如果仍全 vₛ 调 `IsBoundary` 和扫描 incident lists，该项不会自动消失。边界属性与视图无关是源码事实；将其持久化是否划算还需要局部失效和成本验证，本轮没有增加缓存。

<a id="c03"></a>

## C03：已维护索引的前缀读取

`Prefix(limit)` 和 `DonorPool(limit)` 已按有序结构顺序读取前缀，没有复制或重排尾部。分别 O(r)、O(m) 时间和输出空间。`RawCount` 读取 set 大小为 O(1)。当前成本来自**为这两个便宜查询维护完整全序的前置工作**，不能错误归为前缀查询本身复杂。

`DonorPool` 即使没有预算交换需求也可读取；但 `cache` 大小为 0，回收派发早退。Peking 八次回收派发中只有一次真正运行了 64 个回收提案。

<a id="c04"></a>

## C04：接收目录与闭补丁证据构造

源码：[TransactionalProposals.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalProposals.cpp)。一个 root 最多生成 8 个 E/F/H 提案。当前 `Receivers` 先构造完整目录，`Plan` 才逐项 `Fit`，直到第一个成功；未尝试的尾部提案仍承担了构造和析构。

| 函数 | 实际工作 | 成本 |
| --- | --- | --- |
| `Receivers` | 查三边，构建 E；扫描 root 样本选见证，构建 F；查三个顶点边界及邻面，构建 H | root 样本扫描 + 至多 c 次局部 `Prepare` + 局部树查询 |
| 局部 `Prepare` | 排序支持面、复制局部点几何到 map、生成新连接/自由点、计算插值初始高、收集可见样本 | O(d log⁺d+d log⁺v+aᵢ+sᵢ′ log⁺sᵢ′)，sᵢ′ 为收集前可见关联数，可能大于去重后的 sᵢ |
| `VisibleSupport` | 对支持面的全部样本关联检查 Visible，收集可见项后 sort/unique | O(aᵢ+sᵢ′ log⁺sᵢ′)，不能只按可见 sᵢ 计；全不可见也要访问 aᵢ |
| `StrictlyInside` | 样本真实整数比与三边做精确符号比较 | 常数个有理表达式，但有 χ(ℓ) 成本 |
| `Proposal` 拷贝/析构 | maps、面/支持/样本 vectors 的内容复制或逐项释放 | O(d+sᵢ)，树拷贝另含分配；不是 O(1) 小对象 |

八轮两场景均进入 `Receivers` 512 次，Tracy 合计 14.443/9.779ms。`Fit` 实际尝试 2693/3103 次，**不是目录构造数量**。Peking 3103 次中 2061 次 `no_screen_samples`、1029 次 `shape_infeasible`，只有 10 次成功；没有证据时生成/检查目录仍有实付成本。

可复用的事实包括固定快照中的局部连接、样本归属和插值系数；但证据生命周期必须随几何、视图、支持变更失效。收益不能仅由“512 次函数调用”推算，需实际减少 aᵢ、重复定位或未尝试目录构造。

<a id="c05"></a>

## C05：定位、拟合、区间过滤与精确认证

源码：[TransactionalCertification.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalCertification.cpp)、[TransactionalPredicates.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalPredicates.cpp)、`Samples::Weights`。

### 定位和误差链

```text
Fit 或 Donor
  Measure（每个可见支持样本）
    ErrorBounds
      Reference<Interval>：独立双线性源
      CoveringFace
        遍历提案 Faces → Points.at → Weights
      Height<Interval>：插值实际发布几何
      Clip<Interval>：参考和被测投影
      区间误差界；不确定时 ExactError
  Accepts：先比较缓存最大误差区间，必要时才逐样本 ExactError
```

| 函数 | 必须计入的工作 | 上界/分支 |
| --- | --- | --- |
| `Weights` | 固定三边双精度过滤；近边界用真实样本整数比和 cpp_rational 判包含 | 普通 O(1)，不确定分支另计 χ(ℓ) |
| `StoredWeights` | 在已经证明包含后计算三重心权重，不再次精确定位 | O(1) |
| `CoveringFace` | 旧曲面直接用持久 owner；提案曲面逐面查 Points map 并重新判包含 | 旧态 O(log⁺v)；提案 O(fᵢ(log⁺d+C_weights)) |
| `Reference<T>`、`Height<T>`、`Clip<T>` | 固定表达式图；同公式分别运行于区间域和有理域 | 区间域 O(1) 个 outward rounding 运算；有理域 χ(ℓ) |
| `ErrorBounds` | 参考域/近面检查、两次投影和误差上下界；过滤失败可转精确分支 | 一个覆盖查询 + 固定区间表达式 + 可选精确误差 |
| `Measure` | 枚举 sᵢ，累计最大误差区间并缓存 | O(sᵢ fᵢ(log⁺d+C_weights)+sᵢ C_interval+Xᵢ)，早退可减少实际访问 |
| `ExactError` | 同一独立参考、实际几何和透视除法；计算有理平方误差 | 覆盖查询 + χ(ℓ)；不能视作一个普通 double 运算 |
| `Accepts` | 阈值区间平方后比较缓存上下界 | 常见 O(1)；上下界重叠才最多遍历 sᵢ 做精确比较 |
| `Orientation/Shape/Contains` | 浮点过滤，失败后用有理数；Shape 检查三个角 | 固定个谓词 + χ(ℓ)；钝角也进入 Shape 的统一精确分支，不只数值近零时回退 |

test129 `Measure` 含调用 42.96%、`ErrorBounds` 41.36%、`Weights` 33.58%，高度重叠，**不能把三项相加说成超过整帧 100% 的三个独立瓶颈**。`__nextafter` 自身 17.44% 是区间 outward rounding 的真实证据；Boost GCD、整数除法和浮点转有理数是另一条明显下层链。

`WorkLedger::ExactChecks=820/16` 不包括 `Weights/Orientation/Shape` 的全部精确分支。因而“精确计数只有十几次，Boost 不该成为热点”不成立；该计数从未被定义为全程序有理数调用总数。

### Fit 的实际算法

1. 对新连接先做 Shape；随后检查可见样本是否为空。Peking 大部分尝试到这里就返回。
2. 扫描 sᵢ 选旧误差见证，计算一次精确旧误差，用整数平方根得微像素阈值，再扣固定 progress margin。
3. 对每个样本恢复覆盖面和自由高度系数，产生最多四条约束。源码当前既调用 `CoveringFace`，又再次 `Weights`，再扫描 `proposal.Faces` 找系数，重复恢复确实存在。
4. 一个自由高度维护 `[low,high]`；两个自由高度反复裁剪二维可行多边形。空域早退。
5. 选择并写入实际 binary64 高度后，重新 Measure/Accepts；拟合器给出的候选本身不是最终质量证书。

一自由高度的主工作界：

\[
O(f_i C_{shape}+s_i f_i(\log^+d+C_{weights})+s_i)+X_i+W_{Measure}+W_{Accepts}.
\]

两个自由高度再加：

\[
F_2=\sum_{j=1}^{h_i}O(v_j+v_j^2),\qquad h_i\le4s_i.
\]

其中 `ClipPolygon` 每次线性走 polygon 边，随后对输出每个点用线性 `find` 去重，所以有 vⱼ² 项。在通常凸半平面算术模型中 vⱼ=O(j)，得到保守 O(sᵢ³)；真实浮点过程仍按实际 vⱼ 计，不把这个条件界冒充已测到的立方行为。当前未标记每次 ClipPolygon，也没有记录 vⱼ，**89.001ms 的 Fit 合计不能全部归因于裁剪**。

Tracy 中 test129：Fit 89.001ms，扣已标记 Measure/Accepts 后 self 72.682ms；Donor 内 Measure 106.437ms；Fit 内 Measure 约 16.312ms。这些 self 仍包含未标记的 Shape、精确见证、覆盖查询、约束、裁剪和分配，不能再任意分摊。

Accepts 的 8268/64 个导出区间合计仅 0.317/0.066ms。它已经复用了缓存误差上下界；后续只减少其调用，不足以解释数量级总体收益。真正的证据复用应对准构造、定位、Measure 和精确几何前提。

### 未进入本次热路径的认证

`PreservesHeight`/HeightEvidence 支持额外全域高度保护，但冻结配置 `heightGuard=false`，相关四类计数全为 0。不把此功能成本算入已测热点，也不能从关闭它的这轮数据宣称长期视域外质量已受保护。离线 exact polygon DP 不在正常 C++ 路径。

<a id="c06"></a>

## C06：回收环与固定耳切

源码：`Proposals::Ring/Donor`。

- `Ring` 根据 incident faces 的有向对边构造 `map<Identity,Identity>`，再沿关系走环：O(d log⁺d)，空间 O(d)。不是角度排序或全网格遍历。
- `Donor` 收集支持/点/可见样本，反复按稳定 ID 顺序尝试耳。每轮重排剩余环，候选还通过 `find` 定位环位置、Shape 和所有剩余点 Contains。朴素最坏 O(d³(log⁺d+C_predicate))，另计样本收集/排序。
- 成功几何最后做一次完整 Measure。一般回收新面 d−2，固定释放 2 面；没有把中心自由高度再拿去拟合，因为中心将被删除。
- `fast_shape_miss` 是这条固定耳切未找到解，不能解释为全部 one-ring 类别不可行。

test129 的 512 次 Donor 合计 116.100ms，其中扣除 Measure 后为 9.663ms；约 **91.68%** 的 Donor 含调用墙钟落在被标记的 Measure 中。这直接说明本次主要回收成本是证书，不是 ear clipping 的几何写入。

Peking 只有首轮有需求，64 次 Donor 合计 6.115ms。八轮没有 512 次正常回收；不能拿全轨迹平均掩盖这个执行分支差异。两场景 RingVisits 合计 2860/356，EarTests 1612/215；不是最大 degree 的直接观测，也不是全部几何谓词次数。

<a id="c07"></a>

## C07：配对、足迹与全局顺序预留

源码：[TransactionalReservation.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalReservation.cpp)。

| 函数 | 工作 | 成本 |
| --- | --- | --- |
| `Footprint` | 从支持面、新面和自由高构造 reads/writes set；包括面、边、读高度、写高度 | O(d log⁺d) 节点构造，O(d) 空间 |
| `Conflict` | 遍历一侧 writes，查询另一侧 reads/writes，再做反向依赖 | O(d log⁺d)，遇首个冲突早退 |
| `Unite` | 按值复制 receiver 足迹，再插入 donor 足迹 | O(d log⁺d)，不是免费视图 |
| `Plan` 配对环 | 至多 r×m 个 pair，先检查回收认证、Accepts、内部冲突；再 donor 去重、与已预留的 b 个足迹比较 | 保守 O(rm·b·d log⁺d)，另计模糊区间的精确 Accepts |

已执行的 receiver 仍继续检查共同池：`accepted` 的跳过发生在 Accepts、donor Footprint 和内部 Conflict **之后**。这是为当前 D_feasible 分母保留的额外工作，尽管 `diagnostic=false` 也没有完全移出热路径。

test129：9088 次 PairChecks、1332 次 ReservationChecks、499 次 donor reuse、777 次冲突计数，最终 56 个交换。配对/预留墙钟 12.697ms，占完整八帧 8.72%；Accepts 只是其中很小一部分。减少 post-accept 扫描可能减少足迹构造/容器工作，但上游已经算好的 512 个 Donor 不会随之自动省掉。

`D_feasible`、冲突计数、成功事务不是同一个人口。重复 pair 可能累计多次失败；不能把 777/9088 解释为唯一事务的拒绝率。此处当前维持完整审计人口，若转 production early-stop，统计契约也应显式分离。

<a id="c08"></a>

## C08：拓扑物化、样本归属和候选续接

源码：[TransactionalCommit.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalCommit.cpp)、`Samples::Prepare/Publish/Enumerate`。

| 函数/步骤 | 真实维护范围 | 工作界 |
| --- | --- | --- |
| `Commit::Prepare` 收集 | 再核对已认证提案、预算增减、Shape，以及所有局部足迹两两冲突；规范新面连接并排序 | O(b²d log⁺d+bd log⁺(bd)) + 谓词 |
| 槽/身份分配 | 读取本次释放槽和 free-list 前缀；新稳定 ID 在本批排序结果上决定 | O(bd log⁺v)，不扫描活动全表 |
| 邻接准备 | 复制 touched vertex 的 incident list，局部去旧面/加新面，再排序；边记录通过全局 map 查询 | O(bd·d+bd log⁺(n+v+e+bd))；若固定 d 可并入线性局部项 |
| `PreparedTopology::Geometry` | 先查局部目标 map，未覆盖才查旧身份索引 | O(log⁺(bd)+log⁺v) |
| 活动尾交换准备 | 稀疏 positions/ActiveWrites，模拟移除时的 tail swap | O(bd log⁺(bd))，没有复制完整 active array |
| `face_fill` | 已知槽与身份后写独占新面记录 | Θ(bd) 工作，可分块；共享邻接仍在串行阶段 |
| `Commit::Publish` | 局部活动尾交换、索引 node handle 转移、局部点/边替换、推进代际 | O(bd log⁺(n+v+e))，已经预留容量的条件下无全量扫描 |
| `Samples::Enumerate` | 6 组规则样本各扫扩展包围框，再用 Weights 判真实包含 | O(z·C_weights)，不是 O(q) 全域枚举，也不是只按命中样本数计 |
| `Samples::Prepare` | 收集旧支持的样本和接口面；新面枚举/owner 重选/评价；修共享面最大值与受影响 root/donor key | O((aΔ+z+sΔ)log⁺(t+sΔ)+t log⁺(n+v)) + 定位/精确谓词及邻域工作 |
| `Samples::Publish` | 写局部 Values/FaceSamples/Priorities；删除旧 key，插入已准备的节点 | O(sΔ+aΔ+t log⁺(n+v))，旧样本列表释放计入 |

局部修复不是纯 O(b)。test129 八轮仅准备 409 个新面，但进行 **95,837 次位置测试、9,818 个待修样本、1,843 个受影响面**，样本准备独占 22.001ms/15.11% 帧时间。topology support 有界不能抹掉这部分工作。

也不是每个物化事务都保证 O(bd) 单帧延迟：`WorkLedger::Reserve` 触发扩容时搬移原数组，必须加当次搬移规模 Λ；摊销描述不能删除此次 Λ。Peking 八轮 CapacityBytesRelocated=4,317,952，首轮为 3,998,000。这些是受账本覆盖的 reserve 搬移字节，不是进程全部分配或内存流量。

<a id="c09"></a>

## C09：局部网格属性和 Pending 输出

源码：[TransactionalMesh.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalMesh.cpp)。

`Build` 对每个面生成 3 个位置、同面法线、纹理坐标及高度，固定算术 O(1)。`Mesh::Prepare` 先复制旧 Pending，四线程可填写新面属性，再合并/排序去重脏块；`Publish` 写已准备块和活动索引。`Consume` 将排序脏块合并连续区间，且本轮在帧 ROI 外。

\[
W_{mesh}=O(bd\log^+v+(u+bd)\log^+(u+bd))+\Lambda,
\quad Space_{private}=O(u+bd).
\]

若输出消费者延后，u 可以增长，不能默认 u=0。法线必须来自当前拟合几何，不能重新采原高度来省掉真实属性更新。

test129/ Peking 八轮 meshVertices=1227/105，meshIndices=2223/180；四线程面/网格填写派发分别约 0.06～0.08ms 一次，短任务常被同一个线程领完。Peking 首轮 `gtp.mesh.prepare` 约 2.20ms，但内部 mesh-fill 派发仅约 0.073ms，且同帧存在约 4MB 搬移；这支持核查准备/容量，而不是把 2.20ms 全算给法线计算。容量搬移与时序同时出现，尚未单独计时证明其解释了全部剩余。

<a id="c10"></a>

## C10：分块、线程池、等待和工作账本

源码：[TransactionalExecution.cpp](../../../src/experiment/greedy_transactional_lod/TransactionalExecution.cpp)、[DataOrientedRoamThreadPool.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp)。

`Run` 将 count 按商余划成 min(count,p) 个连续区间，各线程写自己的 WorkLedger；任务完成后主线程合并计数/原因/阶段时间。划分按项目数量，不按 sample/约束/精确分支成本。

线程池在锁内一次性入队，notify_all，然后主线程等待“队列空且 activeTaskCount=0”。线程领取一个任务后释放锁，执行任务，再拿锁更新完成计数。短任务没有绑定固定物理线程，因此 4 个 chunk 可以被同一线程执行。

| 符号/区间 | 解释 | 复杂度或限制 |
| --- | --- | --- |
| `std::_Function_handler::_M_invoke` | 可能包含内联后的整个投影、评分或提案循环 | 基础转发 O(1)，但该机器符号自身成本不可全算作 O(1) 包装器；按 C01/C04/C06/C08/C09 的具体 lambda 计 |
| `Run` / `ParallelFor` | 初始化 p 份账本、入队 p 个任务、等待、归约 | 管理工作 O(p) 加原因/时间 map 合并；等待墙钟取决于任务和调度 |
| `pool.enqueue` | mutex、队列节点操作与回调复制 | O(p) 管理，不含后续任务执行 |
| `pool.wait` | 主线程等待全部任务完成 | 不是白烧 CPU 的同义词，不能当作可全部省下的管理税 |
| `pool.sleep` | 池线程等任务，或条件已满足快速返回 | 区间墙钟，不是 off-CPU 精确统计 |
| `pool.complete` | 取得锁、维护计数、必要时通知 | 常数逻辑 + 不确定等待；有些零时长记录未导出 |
| `WorkLedger::Touch/CheckLimit` | 累计样本访问，读取 steady clock 检查限制 | 每次有实际成本；其数量不等于所有 C++ 字段访问 |
| `Merge` / `gtp.ledger_merge` | 固定数量计数相加，再合并 Reasons/Seconds map | O(p·字段数+被合并 key 数×log⁺key 数) |

当前执行模型可条件化写为：

\[
T_{phase}=h_{prepare}+T_{queue}+T_{tasks\ finish}+h_{merge},\qquad
T_{tasks\ finish}\approx\max_{chunk}T_{chunk}+\text{调度间隔}.
\]

它是解释边界，不是已证明的真实 CPU 等式。test129 首轮接收最长任务 12.626ms，主等待 12.670ms，最后任务结束到派发返回 0.043ms，不能把 12.670ms 等待全部认作线程池浪费。

每阶段平衡比 `Σtask_duration/(task_count×max_duration)` 只度量观察到的任务长度分布。OS 抢占也能拉长某任务；未采调度/PMU，不能由此断言全由输入不均或 cache 导致。

<a id="c11"></a>

## C11：标准容器、复制、分配和库维护

完整符号明细区分了 `_Rb_tree` 的 PriorityKey、DonorKey、Identity/slot、Point 和资源足迹等模板实例。读表时不能把所有 `_Rb_tree` 都叫“priority 排序”。

| 函数族 | 算法成本 | 本轮可说 / 不可说 |
| --- | --- | --- |
| `_M_get_insert_unique_pos` / `_M_emplace_*` | 单次 O(log⁺m) 比较，可能分配节点 | 已采到树维护；没有树总插入次数/分配器 cycles |
| `_Rb_tree_insert_and_rebalance` | 平衡维护的一部分 | 不能从它的样本单独恢复全部树搜索成本 |
| `_M_erase` / erase | 清树 O(m)；按 key 删除含 O(log⁺m) 搜索 | `PublishView` 包含旧树清理；祖先关系见明细 |
| vector/std::sort/unique/find | 分别按元素复制、O(m log⁺m)、O(m)、O(m) | 同一符号可能服务不同阶段，需看调用链 |
| `memmove/memcpy/memset` | 抽象 Θ(bytes)，实际吞吐与长度/对齐/机器有关 | 不能把 sample share 当作带宽值，或推总搬移字节 |
| new/delete、malloc/free、merge_chunk | 分配器内部路径，成本依赖大小与分配器状态 | 本轮只有出现位置/事件比例，没有分配次数或缓存因果 |

`CapacityGrowths/Bytes*` 只覆盖明确通过 WorkLedger::Reserve 的容器容量事件，不覆盖 std::map 每个节点、Tracy 自身内存、所有 proposal vectors。不能作为“全部 allocations”的替代指标。

<a id="c12"></a>

## C12：区间舍入、Boost 有理算术与基础数学库

`__nextafter` 对应 interval `Down/Up`；每个区间加/减/乘/除又调用多次。其高占比说明次数与每次成本累积，**不说明可以直接换成普通浮点而保持证书**。

Boost `eval_gcd`、`divide_unsigned_helper`、`eval_multiply/add`、`do_assign_float`、rational_adaptor 转换，以及 `frexpl/scalbnl` 共同服务有理数的构造、规范化和转换。单个静态几何公式中也可能触发许多 bigint 库调用。

- 区间固定表达式：抽象常数次运算；本轮没有 `nextafter` 精确总次数。
- bigint 加/比较/移位通常随操作数 limb 数增长；乘法、除法、GCD、平方根成本还取决于算法分支和位数。统一用 χ(ℓ) 保留，不凭符号名为当前实例捏造位数或固定耗时。
- `sqrt/hypot/frexp/scalbn` 的标量计算在当前参数精度下为固定规模库操作，但不是同一廉价常数。
- `ExactChecks` 不完整，不能用 820 乘一次 bigint 函数平均时间估计总精确开销。

这是 test129 原报告遗漏的关键下层工作。优化必须保存独立参考、保守误差界和精确回退，优先取消重复恢复同一事实，而不是降低精度后仍宣称同契约。

<a id="c13"></a>

## C13：进程/线程外层及未解析符号

`main/Run`、线程启动包装、libc 入口、时钟、系统边界及 `[unknown]` 保留在全函数表。主线程上层 inclusive 不包含别的线程任务，线程包装的高 inclusive 也不等于调度器自身很贵。

对缺失/未知符号不补函数名或复杂度；可以追溯完整记录栈，但不能宣称恢复了全部系统调用图。本次 `cpu-clock:u` 没有内核等待栈和调度事件，不足以判断 off-CPU、页错误或抢占来源。

## 从工作模型到可验证优化条件

1. **只减建序排序：** 必须分别记录 n 面、vₛ 点、incident 访问以及树维护，否则去掉树后资格查询可能仍主导。
2. **只减 dense-Q 投影：** 必须连同暂存初始化、a 关联归约和 q 写回审计；精确 priority 契约不能无证据换成另一指标。
3. **只减配对尾扫描：** 能减少的是 Footprint/Conflict/统计等，不能追认未省掉的上游 Donor/Measure。
4. **只改任务粒度：** 可改善观察到的长度不均衡，但不会减少当前 116,690 次区间过滤或 8,954,405 次视图投影。
5. **只改 topology/mesh fill：** 当前完整帧占比很小，必须先证明完整边界有可见收益，不能仅展示局部倍率。

以上为后续消融应验证的义务，不是收益承诺。没有样本密度、回退率、容器操作和扩容条件，不能从 n 或 b 一个规模参数推导“比 DOD 快多少”。本轮没有拟合复杂模型，也没有修改 GWR 的授权/暂停状态。
