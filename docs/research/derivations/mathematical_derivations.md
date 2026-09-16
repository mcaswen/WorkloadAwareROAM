# 算法数学推导总览

更新：2026-09-17。配套[推导索引与全部 Lean 文件](README.md)。本页将散落推导按模型串成可连续阅读的数学说明；详细证明过程、失败尝试与实验仍以各节原文为准。

**不是一个覆盖全部算法的总正确性定理。** 第 1～8 节整理当前事务算法及候选修订，第 9～12 节保留固定 ROAM 层次等历史模型。它们的几何空间、优先级和状态契约不同，不能直接传递“已证明”状态。每节明确是纸面、Lean 核心、有限证据还是待验证。

第14节新增一般三角化事务的推导与反例，按阶段标注证据；不将历史Lean源文件的检查状态迁移给新模型。

## 1. 统一符号与独立参考

| 符号 | 本页含义 |
|---|---|
| $M,h_M$ | 共形参数域三角网格及其分片线性高度 |
| $h_*$ | 原始栅格高度与冻结双线性插值定义的参考 |
| $Q,Q_V$ | 固定有限采样集合及固定视图的评价子集 |
| $V,\pi_V$ | 相机/分辨率与投影；无效深度单列 |
| $P,G,C$ | 需求紧迫性、接收收益证据、回收损伤 |
| $N,B$ | 实际活动三角形数与硬预算 |
| $r,m,b,d$ | 接收前缀、捐赠池、获批事务数、局部度数 |
| $s_i,a_i$ | 提案内唯一样本数与去重前关联数 |
| $W,D,p$ | 工作量、合法执行依赖下的跨度、线程数量 |
| $I_0,J,\Delta$ | 仅历史层次模型使用：旧/目标事件集及对称差 |

旧文中 $q(t)$ 曾指优先级，本页历史部分统一写 $P(t)$；它与采样位置 $q$ 分开。旧文的 $\Phi$ 也有不同含义，本页质量势函数统一写 $\Psi$。

设栅格单元内坐标为 $(s,t)\in[0,1]^2$，四角原始样本为 $h_{00},h_{10},h_{01},h_{11}$，经冻结高度尺度变换后：

$$
h_*(s,t)=(1-s)(1-t)h_{00}+s(1-t)h_{10}+(1-s)t h_{01}+st h_{11}.
$$

原始资产分辨率、世界尺度及插值规则属于输入。固定对角线三角插值与此一般不相同；最细 ROAM 曲面也不自动等于它。参考选择及校准见[阈值来源记录](../roam_threshold/sources.md)与[当前执行契约](../cpu_refinement/greedy_multipass_contract.md)。

对同一参数点：

$$
e_V(M,q)=\|\pi_V(q,h_M(q))-\pi_V(q,h_*(q))\|_2,
$$
$$
E_{\max}^{Q,V}(M)=\max_{q\in Q_V}e_V(M,q),\qquad
H_{\max}^{Q}(M)=\max_{q\in Q}|h_M(q)-h_*(q)|.
$$

辅助量分别报告：

$$
E_{\rm RMS}^{Q,V}
=\sqrt{\frac{\sum_{q\in Q_V}w_q e_V(M,q)^2}{\sum_{q\in Q_V}w_q}},
\quad
D_{\max}^{Q,V}(R,S)=\max_{q\in Q_V}\bigl(e_V(R,q)-e_V(S,q)\bigr),
\quad U=N/B.
$$

本组质量指标要求评价集合非空，RMS 还要求权重非负且总和为正；空可见集合须报告“无可见评价”，不能当作一次有效的全域零误差测量。权重由评价协议固定，不能从本公式反推现有报告已经采用某种权重。$D_{\max}$ 是有符号的最大逐点差，不是两张网格之间的几何距离，也不替代绝对误差。有限 $Q$ 给出 sampled maximum，不能称为连续曲面上确界。运行时 $Q$ 与独立评价集合的差异见[QPC-04C](../cpu_refinement/qpc_04c_quality_target_results.md)。

## 2. 从严格反馈到快照贪心：改变了什么

**状态：执行契约，非全局最优性定理。** 原文：[GMP 契约](../cpu_refinement/greedy_multipass_contract.md)。

传统严格控制器在每次修改后重新观察请求；当前算法对同一批次起始状态生成全部合法需求并形成稳定全序：

$$
I_1\succ I_2\succ\cdots\succ I_\ell.
$$

随后按顺序尝试有限接收目录和固定捐赠池，接受首个满足认证、资源、预算条件的方案。已经成功预留的资源不被后续需求抢占，失败继续下一项；本批新生候选只在下一快照可见。

历史复合紧迫性为：

$$
P(t)=\max\bigl(E_{Q,V}(t),\beta\ell_V(t)\bigr).
$$

QPC-04D 实验只在原资格人口内改为误差优先，不改变原资格阈值。因而：

$$
\text{换排序}\not\Rightarrow\text{产生原资格域之外的新请求}.
$$

“全局”表示基于同一完整快照的共同人口和全序，“贪心”表示不可回退地优先尝试资源。它不等于 Legacy 动态请求顺序，也不求解固定预算下全局最优剖分。$P$、具体提案的 $G$ 和捐赠方 $C$ 必须分开；需求不能由捐赠可行性反向筛选后再作为共同分母。

## 3. 局部原语、度数与硬预算

**状态：纸面条件证明，生产有限核查另见 [04B](../cpu_refinement/qpc_04b_boundary_integration_results.md)。** 原文：[CR 推导](../cpu_refinement/cavity_budget_recovery_derivation.md)。

内部点 $v$ 的 $d$ 个邻面组成简单环多边形。删除中心、保留全部环顶点且不新增内部点，合法剖分恰有 $d-2$ 个三角形，所以：

$$
d\longrightarrow d-2,\qquad \Delta N_{\rm donor}=-2.
$$

这是面数结论；最小角、质量和数值可行性仍需认证。最小角 $\theta_0>0$ 对全部输入/输出成立时，围绕内部点的角度和为 $2\pi$：

$$
d\theta_0\le2\pi
\Longrightarrow d\le\left\lfloor\frac{2\pi}{\theta_0}\right\rfloor.
$$

历史 30° 例子的界是 12；生产契约 $\theta_0=\arctan(1/3)$ 对应 19，不能混用。这个界限制拓扑记录，**不限制补丁样本数 $s_i$**。

当前接收成本 $c_i\in\{1,2\}$，分别允许单侧边界细分与原内部细分；净零翻边另计。设 Free 的命名额度总数不超过 $B-N_0$，配对捐赠方不复用、删除面不冲突：

$$
N_{\rm final}
=N_0+\sum_{\rm Free}c_i+\sum_{\rm Paired}(c_i-2)\le B.
$$

证明直接来自第一项受空额限制、第二项每对非正。全旧目录时 $c_i=2$，退化为 $N_0+2|\mathrm{Free}|$。新旧存储的私有峰值不在活动面数公式内；需要单列分配费用和发布原子性。

## 4. 几何证据：高度拟合与 one-ring oracle

### 4.1 一个高度自由度

**状态：纸面证明与有限核查；历史自由度不代表当前允许改全部旧点。** 原文：[高度 minimax](../cpu_refinement/local_height_minimax_derivation.md)。

固定连接后，新高度场可写成 $h_a=h_0+\varphi a$。对样本 $i$ 定义 $f_i=h_*(q_i)-h_0(q_i)$、$\beta_i=\varphi(q_i)\ge0$。给定高度误差阈值 $\tau$：

$$
|f_i-\beta_i a|\le\tau.
$$

$\beta_i>0$ 时等价于区间：

$$
\frac{f_i-\tau}{\beta_i}\le a\le\frac{f_i+\tau}{\beta_i}.
$$

$\beta_i=0$ 时必须直接检查 $|f_i|\le\tau$。全部区间及允许高度域相交非空，当且仅当固定自由度内存在可行解。系数已给定时一次归约工作 $O(s)$；构造系数、定位和最终舍入后认证不免费。

无高度域限制时，可由任一下界不超过任一上界推得：

$$
F^*=\max\left\{
\max_{\beta_i=0}|f_i|,
\max_{\beta_i,\beta_j>0}
\frac{|\beta_j f_i-\beta_i f_j|}{\beta_i+\beta_j}
\right\}.
$$

此处空约束子集的最大值取 0。该 $O(s^2)$ 公式用于审计，不作为默认运行时算法。旧最大误差为正、零增量处是允许域内点时，单自由度严格改善要求全部活动极值有正 $\beta$ 且残差同号；否则相反方向要求或零权重形成阻塞。一般少量自由度的判据与反例保留在原文。

### 4.2 固定视图的透视阈值

沿高度轴投影齐次坐标为 $X=X_b+c_x z$、$Y=Y_b+c_y z$、$w=w_b+c_w z$。将网格与参考投影通分，在参考深度和允许新深度都严格为正时，单点误差具有：

$$
e_i(a)=K_i\frac{|f_i-\beta_i a|}{d_i+b_i a},
\qquad b_i=c_w\beta_i,\quad d_i+b_i a\ge w_{\min}>0.
$$

$K_i\ge0$ 是同一参数点参考与相机确定的系数。若像素缩放为 $s_x,s_y$、参考深度为 $w_i^*>0$，则：

$$
K_i=\frac{\sqrt{[s_x(c_xw_b-c_wX_b)]^2+
[s_y(c_yw_b-c_wY_b)]^2}}{w_i^*}.
$$

乘以正分母，将绝对值拆成两式：

$$
(-K_i\beta_i-\tau b_i)a\le\tau d_i-K_i f_i,
$$
$$
(K_i\beta_i-\tau b_i)a\le\tau d_i+K_i f_i.
$$

加上深度与允许高度域即可作线性可行性判断。必须按系数符号处理上下界；此结论不覆盖遮挡图像误差或随提案改变的采样集合。求解器返回的高度还需对实际数值认证，不能把符号可行直接当作浮点实现正确。

### 4.3 固定环的局部最小最大 DP

固定环顶点坐标、高度与参考。合法三角形 $T$ 的有限样本成本为 $c(T)$；它不依赖其他面如何连接，故剖分成本为 $\max_T c(T)$。

沿边 $(i,j)$ 的第三点枚举 $k$，任一剖分唯一分成该三角形与两个较小子多边形：

$$
F(i,j)=\min_{\substack{i<k<j\\\mathcal A(i,k,j)}}
\max\{F(i,k),F(k,j),c(i,k,j)\},\qquad F(i,i+1)=0.
$$

无合法项取 $+\infty$。对子多边形大小归纳：任一合法解对应一个枚举项，任一有限枚举项也能合法拼接，因此递推给出该操作类别内的最优值。

给定合法性与成本表，DP 工作 $O(d^3)$、表空间 $O(d^2)$。完整费用为：

$$
W_{\rm cavity}=W_{\rm ring/visibility}
+\sum_TW_{\rm certificate}(T)+O(d^3)+W_{\rm reconstruction}.
$$

朴素逐三角形扫描样本可另达 $O(d^3s)$。这个 oracle 只区分快路径漏解和固定环类别内无解，不证明任意局部重构无解，也不是当前生产默认。

## 5. 事务组合：几何独立与状态独立

**状态：条件纸面证明与有限正反序检查。** 原文：[批次推导](../cpu_refinement/transaction_batch_derivation.md)。

定义事务 $i$ 的完整读取资源 $R_i$ 和写入资源 $W_i$。直接独立要求：

$$
W_i\cap(R_j\cup W_j)=\varnothing,\qquad
W_j\cap(R_i\cup W_i)=\varnothing.
$$

资源包括几何、边关联、身份存在性、证书依赖以及必要的状态维护。仅删除面不交不足以证明独立。允许的共享派生贡献必须有单独的确定性归约规则；浮点法线求和不能直接当成结合运算。

**交换性证明。** $j$ 不写 $i$ 的读取资源，故先执行 $j$ 不改变 $i$ 的局部结果；反向同理。不同写资源互不覆盖，派生贡献按共同规则合并。任意排列可通过相邻交换得到，故最终逻辑核心与续接投影一致。

此证明依赖完整足迹、唯一身份、固定外接口和最终状态确定修复；它不证明任意无锁容器安全。

两个删除中心的 **incident-triangle sets** 共享一个三角形，当且仅当两中心之间有网格边。该结论不是标准 closed star 的交集命题，也不是完整事务冲突判据。

## 6. 旧 sampled-maximum 保证及其失败边界

**状态：窄条件纸面保证；不足以保证持续质量已有自然反例。**

设旧接收域见证下界 $L\le E_{\max}^{Q,V}(M)$，接受目标 $\tau=L-\eta$。完整修改支持上新误差均不超过 $\tau$，域外不变，则：

$$
E_{\max}^{Q,V}(M')\le E_{\max}^{Q,V}(M).
$$

推导只需对改动点和域外点分情况，不需要每个点都改善。因此它允许：

$$
e(q_1)\downarrow,\quad e(q_2)\uparrow,
\quad e_{\rm new}(q_2)\le E_{\max,\rm old}.
$$

它不约束离屏几何、RMS、未来视图或另一个评价集合。固定存活点也不约束删除中心后的插值。04B/04D 的误差转移没有反驳这个窄命题，而是证明它不足以支撑当前持续质量目标。

## 7. QPC-04E 候选：从损伤条件到可组合进展

**状态：纸面推导、QC-01～03抽象Lean核心与71个原批准交换的有限审计完成；尚非生产政策。** 完整过程、反例、运行时义务见[QC-01～09](../cpu_refinement/quality_contract_derivation.md)，[实际结果](../cpu_refinement/qpc_04e_quality_contract_results.md)中三个冻结参数组合保留非平凡机会，但Canyon原批全部高度失败，持续质量与请求机制仍未解决。

令 $a=e^2$、$u=|h_M-h_*|^2$，固定目标平方 $c_E,c_H$。候选要求完整修改支持满足：

$$
a_{\rm new}(q)\le\max(a_{\rm old}(q),c_E)\quad(q\in Q_V),
$$
$$
u_{\rm new}(q)\le\max(u_{\rm old}(q),c_H)\quad(q\in Q).
$$

核心恒等关系为：

$$
y\le\max(x,c)\iff[y-c]_+\le[x-c]_+.
$$

按 $x\le c$ 与 $x>c$ 分情况即可证明。于是：

$$
\Psi(M)=\sum_{q\in Q_V}[a_M(q)-c_E]_+,\qquad
\Psi(M')\le\Psi(M).
$$

该条件允许目标以下退化，所以正确屏幕界是：

$$
E_{\max,\rm new}\le\max(E_{\max,\rm old},E_*).
$$

高度条件跨轮归纳利用 $\max(\max(x,c),c)=\max(x,c)$：

$$
u_t(q)\le\max(u_0(q),c_H).
$$

这阻止固定采样点上的高度超标损伤逐轮累加，不保证消除种子超标或任意视图的小像素误差。

无冲突提案的改变支持可分区，共享接口贡献不变并去重时：

$$
\Delta\Psi_{\rm batch}=\sum_i\Delta\Psi_i.
$$

要求每个质量交换 $\Delta\Psi>0$ 可排除无进展消耗，但不能推出有限恢复。如果另有固定 $\delta>0$ 下界，且视图/样本/目标均固定，才有：

$$
K_{\rm successful}\le\lfloor\Psi_0/\delta\rfloor.
$$

此界不限制失败搜索或等待；目录无解仍可能停在 $\Psi>0$。净收益单独不足：目标平方 1 时，$(9,0)\to(4,4)$ 使 $\Psi:8\to6$，却制造新超标点，逐点条件必须保留。

## 8. 局部续接与完整成本

**状态：纸面支持论证加源码费用模型；不是已证明性能最优。**

令 $F$ 包含删除面及所有改高点的邻面，$Q_F=Q\cap\mathrm{closed}(\cup F)$。若 $q\notin Q_F$，覆盖几何不变；$q\in Q_F$ 则需在新面及保留接口面中恢复闭面关联与唯一 owner。旧 owner 在外侧的共享边样本也必须纳入，不能只看删除面拥有的样本。

完整工作量按职责计费：

$$
W_{\rm frame}=
W_{\rm view}+W_{\rm priority}+W_{\rm proposal}
+W_{\rm certificate}+W_{\rm reservation}
+W_{\rm topology}+W_{\rm continuation}+W_{\rm mesh}
+W_{\rm synchronization}+W_{\rm allocation}.
$$

当前全样本视图刷新仍含 $\Theta(|Q|)$ 投影及关联归约。提案几何证据的直接界为：

$$
W_{\rm evidence}
=\sum_iO(a_i\log(a_i+1)+s_i d_i)+W_{\rm lookup}+W_{\rm exact}.
$$

有序集合且逐既有事务查冲突时，$r m$ 配对、$b$ 预留、足迹规模 $f$ 可给出模型项：

$$
W_{\rm reservation}=O(rm(b+1)f\log(N+2))+W_{\rm ambiguous}.
$$

资源索引可能消除对 $b$ 的重复扫描，却不自动消除 $rm$、样本认证或续接。它仍属待验证方案。两变量裁剪当前线性去重的保守坏界为 $O(s_i^3)$；不能用纸面理想 $O(s_i^2)$ 取代源码账本。细目见[阶段复杂度](../cpu_refinement/transactional_algorithm_overview.md)。

对已正确建立的计算 DAG，单位处理模型给出：

$$
T_p\ge\max(W/p,D),\qquad T_p=O(W/p+D)
$$

后式要求理想贪心调度及相应任务费用模型，不保证当前线程池实现。递归调用顺序不能直接当成必要 $D$；新增的建图、发现、认证与维护也必须计入 $W$。

## 9. 历史模型一：固定依赖、阈值与激活谱

**状态：抽象代数核心已由 Lean 检查；几何对应和成本为纸面。** 原文：[模型与证明](../roam_threshold/model_and_proofs.md)、[激活谱](../roam_threshold/algorithm_and_limits.md)。

在固定有限事件关系上，请求指向前置，允许零步可达：

$$
\operatorname{cl}(A)=\{v:\exists r\in A,\ r\leadsto v\}.
$$

零步路径给扩张性，保持起点给单调性，路径拼接给幂等性，起点属于哪一集合给：

$$
\operatorname{cl}(A\cup B)=\operatorname{cl}(A)\cup\operatorname{cl}(B).
$$

单三角形伙伴关系可有环；收缩菱形后才得到按深度的 DAG。上述并集等式只需固定关系，不需无环。非负闭包成本：

$$
C(A)=\sum_{v\in\operatorname{cl}(A)}c_v,\quad
C(A\cup B)+C(A\cap B)\le C(A)+C(B).
$$

证明还使用 $\operatorname{cl}(A\cap B)\subseteq\operatorname{cl}(A)\cap\operatorname{cl}(B)$，所以这是成本次模性，不是最大误差收益可加性。一般双前件闭包 $a\land b\Rightarrow c$ 不满足并集等式，已有 Lean 反例。

若父子优先级单调 $P(child)\le P(parent)$，定义完整潜在域的必选集 $R_\tau=\{t:P(t)>\tau\}$。沿父链逐层排除超标叶，得到：

$$
\max_{t\in L(I)}P(t)\le\tau\iff R_\tau\subseteq I.
$$

对只细化旧状态的目标：

$$
J_\tau=\operatorname{cl}(I_0\cup R_\tau),\qquad
N_{\min}(\tau)=N_0+\sum_{v\in J_\tau\setminus I_0}c_v.
$$

若触及禁止事件则取 $+\infty$。闭包最小性与非负权重给出：

$$
\exists\text{合法细化目标满足阈值和预算}
\iff N_{\min}(\tau)\le B.
$$

再定义：

$$
a(v)=\max\bigl(\{P(r):r\leadsto v\}\cup\{0\}\bigr).
$$

存在超阈值起点等价于 $a(v)>\tau$，所以闭合 $I_0$ 下：

$$
J_\tau=I_0\cup\{v:a(v)>\tau\}.
$$

这就是依赖饱和最大值的激活表示，不当作新理论。评分并非独立几何误差；模型也不能自动合并远处网格换预算。

完整潜在层次数量可达 $\Theta(2^H)$。增量维护的条件界：

$$
T_{\rm frame}=T_{\rm source}+O((s+k+1)\log n+|\Delta J|).
$$

它仍含发现变化源的 $T_{\rm source}$；显式全谱或无有效跳过证书的黑箱优先级最坏需要 $\Omega(n)$。[增量 Gate](../roam_threshold/incremental_gate.md)已 No-Go，不是对所有隐式结构的下界。

## 10. 历史模型二：已知目标的端点物化

**状态：支持/队列核心 Lean；几何与数值计数纸面。** 原文：[目标物化](../roam_parallelism/target_materialization_gate.md)。

给定旧/目标合法闭合集 $I_0,J$，$\Delta=I_0\triangle J$、$k=|\Delta|$。叶谓词只读自身和父事件：

$$
\ell_I(t)=(root(t)\lor parent(t)\in I)\land t\notin I.
$$

两项都未改变则叶资格不变，取逆否：

$$
L(I_0)\triangle L(J)\subseteq
\Delta\cup children(\Delta),\qquad
|\Delta\cup children(\Delta)|\le3k.
$$

合并组只读组内事件与直接孩子，故受影响组包含于：

$$
\mathcal C_\Delta=group(\Delta)\cup group(parent(\Delta)),
\qquad|\mathcal C_\Delta|\le2k.
$$

两个严格共形 cut 的共同叶保留，变化区域接口可从旧邻接继承；新边与接口排序配对需 $O(k\log(k+1))$ 工作。完整成本仍包含身份/深度查询、索引和其他续接义务。

抽象标记的端点规则为：

$$
Split'=Split_0\cup(J\setminus I_0),\quad
Merged'=Merged_0\cup(I_0\setminus J),
$$

历史与 blocked 保持。固定评分函数下，支持外完整逻辑条目一致，因此：

$$
\operatorname{Repair}(\operatorname{Support}(I_0,J),
\operatorname{Entry}(I_0),\operatorname{Entry}(J))
=\operatorname{Entry}(J).
$$

对应 Lean 的 `repair_split_queue_exact`、`repair_merge_queue_exact`。它们不包含中间 churn、动态评分或原生失败再接纳的全部义务。$3k/2k$ 计数不是 Lean 已检查项，只有支持逻辑包含被检查。

物化成功不消除目标发现：

$$
T_{\rm complete}=T_{\rm discover}+T_\Delta+T_{\rm materialize}.
$$

当前事务算法不再要求 Legacy 提供 $J$；因此本节是历史模型，不是其目标发现正确性的证明。

## 11. 历史模型三：候选出生与最小修改量

### 11.1 同请求的条件队列替换

原文：[候选出生证明](../roam_parallelism/native_candidate_birth_proof.md)。对象是原生逻辑登记成员，不等于所有几何上可能的候选。

局部转移枚举集合 $BirthSet(op)$ 须覆盖新登记、重新接纳与改键。保持以下不变量：每个当前逻辑成员都有当前有效版本条目，被接受的有效条目也确为当前成员。则弹掉旧版本后，在相同分数/稳定身份全序下：

$$
NextRequest_{\rm versioned}(S)=NextRequest_{\rm legacy}(S).
$$

证明先由覆盖与可靠性得到同一极值，再以相同控制/预算和转移规则归纳。它没有证明资格/邻域工作可全部删除；失败再接纳反例阻止过小的出生规则。

输出 $\Delta/\Gamma$ 可以在线归约，但若中间更新很多，其费用仍由真实变化和失败生命周期控制，不能写成整个发现只需 $O(|\Delta|+|\Gamma|)$。详见[工作削减推导](../roam_parallelism/native_target_discovery_reduction_derivation.md)。

### 11.2 等成本最小修改量

原文：[暂存记录](../roam_recourse/minimum_change_note.md)。固定前置模型、等事件成本、旧合法集合 $J_0$、已知必选闭合集 $C$、容量 $b$，令 $u=|C\setminus J_0|$：

$$
R_{\min}=u+\max(0,|J_0|+u-b).
$$

必须加入 $u$ 个事件；超额至少删相应数量。在闭合集 $J_0\cup C$ 中移除不属于 $C$ 的可移除最大事件可达到下界。它只优化修改数量，不创造并行度，不含 $C$ 的发现，当前暂存且无 Lean。

## 12. 历史成本语义：保留了哪些公式

**状态：PCSG 在完整拓扑同任务审计处提前结束，没有完成三阶段成本分类或 Lean 工程。** 原文：[参数化成本语义 Gate](../roam_parallelism/parametric_cost_semantics_gate.md)。

对固定输入 $x$、无重复身份集合 $I$，每项只写唯一位置的 $f(x,i)$。将 $I$ 分成互不交的块，顺序与独立分块执行中每个身份都恰写一次相同值；域外不写。故二者逐位置相同，之后相同确定性后处理也保持相等。

令 $\mathbf e_Q,\mathbf e_M$ 为评分与脏写原语的单位计数向量，则仅就内核：

$$
\mathbf W_{S,Q}^{\rm kernel}
=\mathbf W_{A,Q}^{\rm kernel}=n\mathbf e_Q,\qquad
\mathbf W_{S,M}^{\rm kernel}
=\mathbf W_{A,M}^{\rm kernel}=d_{\rm dirty}\mathbf e_M.
$$

这里 $d_{\rm dirty}$ 是脏条目数，不是前文顶点度数。相同单位成本 $c$ 下，块 $j$ 有 $m_j$ 项，其跨度为 $m_jc$，并行组合跨度为 $\max_jm_jc$。只有平衡块长得到证明后，才可代入 $\lceil n/b_{\rm blocks}\rceil c$；块数不是处理器数。

外层建堆、脏计划、排序、容量调整、派发和最终维护仍计入完整阶段。内核工作相等不能直接推出完整阶段工作相等或存在性能交叉点。

另一个保留的条件下界是：如果一个完整同任务批量程序必做 $n$ 次成本 $c_S$ 的扫描，而有竞争力的串行程序完整时间有上界 $U_S$，则：

$$
T_A(p)\ge\frac{nc_S}{p},\qquad
nc_S\ge pU_S\Longrightarrow T_A(p)\ge U_S\ge T_S.
$$

它能在前提成立时排除严格加速。但当时尚未构造成本完整的批量拓扑程序，也没有确立所需串行上界；因此没有继续代入任意常数画非空“可行区”，更没有把它当成 CPU-CBT 失败的证明。后续目标物化工作单独填补部分状态构造问题，不能追认旧 Gate 已通过。

## 13. 公式到证据的最终检查

| 结论 | 原始证据 | 禁止外推 |
|---|---|---|
| 固定依赖闭包/阈值/激活 | 4 个 Lean 文件及几何纸面模型 | 真实 C++、任意评分的几何最优性 |
| 端点支持与队列修复 | 2 个 Lean 文件及物化论文式记录 | 快速目标发现、当前事务几何质量 |
| one-ring、拟合、批次 | 纸面条件与有限核查 | 所有补丁可行、长期误差恢复 |
| 原旧接受条件 | 同视图同 Q 的最大值推导 | 逐点、离屏、未来视图不退化 |
| QPC-04E 新条件 | 纸面推导、QC-01～03参数化Lean、71交换只读审计 | 具体浮点/几何形式证明、生产接入、持续质量已解决 |
| 工作/跨度公式 | 声明计算与存储模型 | 当前 CPU 必达倍率或已无优化空间 |

新增阶段继续补“前提→中间推导→反例→运行时义务→成本”，保持与[总目录](README.md)和具体小规划互链。不要只向本页追加没有来源和状态的漂亮公式。

## 14. 一般资源约束三角化事务：立项边界

**状态：ATT-01条件拼接和有限实例完成，资源/质量层尚待后续。** 原文与中间步骤见[模型推导](../adaptive_triangulation/model_derivation.md)、[操作嵌入](../adaptive_triangulation/operation_embeddings.md)，范围见[大规划](../../plans/adaptive_triangulation/resource_constrained_transaction_theory_plan.md)。

给定局部替换 $K_i\to K_i'$，实际面数差 $\delta_i\in\mathbb Z$，完整资源集 $R_i,W_i$，证书 $\mathcal C_i$ 和优先级 $P_i$。这里需要先区分完整补丁边界和连接未修改网格的接口 I：

$$
M=U\cup_I K,\qquad M'=U\cup_I K'.
$$

相同边界点集合不充分；需接口连接、身份、方向、必要属性和局部流形/几何证据。对圆盘，由欧拉式和边面关联计数：

$$
(v+b)-(e+b)+f=1,\quad 3f=2e+b
\Longrightarrow f=2v+b-2.
$$

所以完整边界剖分固定时 $\delta=2\Delta v$，只能有偶数面数差。当前外边界 +1 细分须单列自由外边界扩展，不能直接归入 $\partial K=\partial K'$ 严格类。

如果删除面唯一、新增身份相容，集合计数给出：

$$
N'=N_0+\sum_{i\in\mathcal B}\delta_i\le B_{\max}.
$$

这只约束终点；满预算时 +2/−2 先加后减会中间越界，失败后只保留 +2 也越界。整批可见发布、合法顺序前缀及失败子集复核须明确；临时内存另计。

固定观察集 Q、环境、权重 $w_q\ge0$ 与目标 $\tau_q\ge0$，则逐点损失条件：

$$
\ell(M',q)\le\max(\ell(M,q),\tau_q)
\Longrightarrow
w_q[\ell(M',q)-\tau_q]_+\le w_q[\ell(M,q)-\tau_q]_+.
$$

逐项相加可得加权超标势不增。这是QC代数的候选扩展；非负损失不自动局部，批次还需证书迁移或独占损失变化支持。例如两个字段分别从0改1，损失 $(x+y-3/4)^2$ 单独从 $9/16$ 降至 $1/16$，合并却升至 $25/16$；补全证书读集后应发现冲突。

因此新方向需分别建立：局部几何可拼接、实际更新读写完备、整数记账、一般损失支持、质量组合和确定续接。安全结论可能完全不使用 P，不能据此声称已得到贪心最优性、恢复时限或新的高性能一般求解器。
