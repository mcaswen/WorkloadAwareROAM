# 最小参考曲面校准：代码事实

> 日期：2026-09-11
> 范围：GATE-02 公共嵌套采样 k=0/1/2、双线性主参考与限定自然校准
> 规划：[GATE-02](../../plans/ordering_relaxation/gate_02_mesh_quality_evaluation_plan.md)
> 审查：[实现与参考校准审查](../../reviews/ordering_relaxation/gate_02_mesh_quality_evaluation_review.md)

> 已确认决策：主参考锁定为原始高度样本加双线性插值；最小参考校准通过，完整质量评价按需暂停。此状态不改变下文已实现/未实现能力的区分。

## 1. 文件与依赖

以下除显式标注外均为 FACT。

| 文件 | 符号与当前职责 |
| --- | --- |
| `src/experiment/mesh_quality/MeshQualityEvaluator.h` | `BilinearHeightfieldReference`、`EvaluationStatus`、`QualityView`、`QualityOptions`、`QualityLocation`、`QualityResult`、两个 `EvaluateMeshQuality` 重载、状态 `ToString`；只依赖 GLM/标准库，前向声明 TerrainMeshData |
| 同目录 `MeshQualityEvaluator.cpp` | 私有 `SurfaceQuery/QueryNode/QueryHit`、参考共享边归属、`BarycentricPoint/SubdivisionSamples/AdditionalSamples`、分层样本流、几何/投影判定和最大值归约；依赖 TerrainMeshBuilder.h 的网格值类型，不调用构建器或 ROAM 评分 |
| `tests/MeshQualityEvaluatorTests.cpp` | `Quad`、`LoadReferenceSamples`、`CheckBilinearReference`、`CheckAnalyticGeometry`、`CheckFailures`、`CheckRealRefinement`、`DepthHistogram`、`FinestSettings`、`SaveBuild`、`Calibrate`；拥有来源、原始样本、实际构建器和文件输出 |
| `tests/CMakeLists.txt` | `parallel_roam_MeshQualityEvaluator_tests` 与 CTest `MeshQualityEvaluator`；链接既有 DOD/formal input、七个 Classic builder 实现、地形构建/加载、视图与计时工具 |

命名空间 `ParallelRoam::Experiment::MeshQuality`。当前唯一调用者是新测试目标；生产算法、app、渲染器、旧 formal schema 和根 CMake 源清单没有修改。评价器不依赖 DOD State、Classic 节点池或 `ComputeScreenErrorScore`。

调用为同步普通函数；调用方保证网格在调用期间不变。函数局部持有查询索引和参考归属数组，返回结果拥有数值与位置，不保存外部引用。没有后台任务、共享缓存、线程池使用或 GPU 资源。

## 2. 公共数据与状态

`BilinearHeightfieldReference` 借用 `span<const uint16_t> Samples`，持有 Width/Height、double TerrainSize/HeightScale。调用方负责原始样本生命周期；默认空样本/零尺寸不能求值。主参考不依赖生产 `HeightMap::SampleBilinear`。两个重载分别接受 `(三角参考, measured, view, options)` 与 `(双线性参考, UV模板, measured, view, options)`，共同进入私有 `EvaluateSamples`；旧三角重载用于 LEGACY 诊断和解析夹具。

`QualityView` 保存原 float `ViewProjection` 和非零 Width/Height；内部投影转 double。矩阵列存在非有限值或 double 行列式为零时返回 `InvalidView`。

`QualityOptions` 默认上限为 40,000,000 样本及 600 秒，SamplingLevel 默认 0、仅支持 0/1/2。零样本配额、非正或非有限秒数、层级大于 2 直接返回 `ResourceLimit`；实际采样逐点检查数量，每 1024 点检查总耗时。索引构造不能中途抢占，外部进程监测承担硬资源上限。

`QualityResult` 包含：

- `Status`：`Sampled` 映射为 `sampled_only`，仅表示指定 SamplingLevel 采样完成；结果同时保存该层级，其余状态为 `InvalidGeometry`、`InvalidView`、`Incomplete`、`ResourceLimit`。没有连续极值、收敛通过或 Gate 通过状态。
- `SampleCount`：实际发出的参考样本数，含被测查询失败的点；`ScreenSampleCount` 在被测查询前由参考点是否处于 NO 视锥确定，不因覆盖缺失而缩小。
- MissingCoverage、AmbiguousCoverage、InvalidGeometry、InvalidProjection、NearPlaneCrossing 五类计数。覆盖和投影异常使整个结果 `Incomplete`，不冒充拓扑检查结论。
- `SampleHash`：对参考样本种类与索引 ID，按固定字节顺序累计 FNV-1a。必须连同参考身份使用，不单独标识几何、相机或完整实验。
- `SampledScreenMaxPx`、`SampledHeightMax` 是 optional；没有有限可求值点时为空，已有有限值在不完整/超限时保留。
- 两个 `QualityLocation` 保留样本序号、UV、参考/被测世界位置。只有对应 optional 存在时位置才有定义；默认零位置不能当有效最大点。
- IndexMilliseconds 包含输入几何核对、被测索引、局部采样模板和参考归属准备；SampleMilliseconds 包含指定层遍历、查询、哈希和归约；TotalMilliseconds 为函数进入到显式返回前，局部容器析构和外部文件输出在进程总墙钟内。

当前公共类型支持指定 0/1/2 层的独立评价，没有 RMS、配对 D_max 或自动收敛结论。PLANNED：k=3、参考射线查询、RMS、`sampledPointwiseExcessMaxPx` 均未实现；后者只在研究契约中定义，不由两个全局最大值相减代替。test129 的经验稳定判定由离线报告对比三层结果，不进入生产或自动扩展采样。

## 3. 输入检查与域点查询

`InvalidTriangles` 检查索引完整性/范围、使用到的 Position/UV 有限性及 UV 非退化。参考空网格非法；被测空网格允许进入查询并产生逐样本 missing coverage。退化门槛为 `32×double epsilon×(|ex fy|+|ey fx|)`，不按固定面积删除高分辨率合法三角形。

双线性入口对采样模板传入 `checkPositions=false`，不检查/读取模板 Position 来定义参考；被测位置检查始终保留。`ValidSource` 要求原始宽高至少 2、样本数恰好为宽高乘积、TerrainSize 有限且正、HeightScale 有限、模板所有 UV 有限且位于 `[0,1]²`；失败返回 `InvalidGeometry`，不能以夹紧隐藏越域输入。

`SurfaceQuery` 引用已检查的被测网格，持有三角形编号重排数组与 UV AABB 树。包围盒长轴同分选 x，按三个顶点坐标和的中位数分组，同分以三角形编号决定；叶最多八个三角形。当前没有世界 AABB，因为首层校准不需要射线。

`FindInNode` 按包围盒筛选，重心坐标容差为 1e-10，包围盒 padding 为对应跨度的 2e-10。容差内负权重归零后归一化，明显越界点不夹回曲面。插值实际 Position，不重新调用 HeightMap。

查询遍历全部命中。共享边/顶点的相同位置以原三角形最小编号选归属；多个命中只要涉及严格内部点，或位置差超过 `1e-8×max(1,两位置长度)`，标记歧义。容差没有根据自然校准结果修改。

该查询用于当前单值高度地形，不是全局拓扑验证器；不能通过有限样本无歧义证明没有任何洞或重叠。实际校准网格另执行原拓扑/队列验证，参考使用已知规则构建器。

## 4. 参考采样和指标

`EvaluateMeshQuality` 固定遍历每个参考三角形的顶点、边中点和重心。顶点以索引的最小 incident triangle 归属去重；边以排序后的端点索引组成 uint64 key，取最小 triangle owner；重心以原三角形编号标识。

归属表只随参考变化，被测网格不参与选点。当前去重基于参考索引，不进行按坐标焊接；自然参考 `TerrainMeshBuilder::Build` 已让共享格点共用索引。若调用者传入重复顶点的另一种参考存储，不能沿用自然参考的去重样本身份或数量主张。

每个样本插值模板 UV，在被测网格相同 UV 上查询。双线性重载通过 `SourcePosition` 从原始四角 uint16/65535 double 高度独立求值：u/v 乘宽高减一，横向后纵向插值，端点 1 使用末格点，最后施加地形世界尺寸/高度比例。模板 Position 不参与运算；LEGACY 三角重载仍直接插值参考 Position。高度最大值为完整采样域上的 `|measured.y-reference.y|`；屏幕域用本次参考位置重算，仅接纳齐次 NO 六面视锥且 w>0 的点。

被测投影 w<=0 或 z<-w 记录 near crossing；其他方向移出视口仍使用未夹紧 xy/w 计算二维像素距离。参考无视锥内样本时屏幕 optional 为空，高度仍保留。缺失/歧义点不生成假误差，也不能因为不参与有限最大值就把配置标为完整。

首层 k=0 没有连续域或三层稳定性保证。当前没有 RMS 分母、可见参考射线或时间稳定性指标。

新增 k=1/2：私有 `BarycentricPoint` 是三个整数、公共分母 24；`SubdivisionSamples` 对原三角形递归四分并发出子单元顶点/中点/重心，`AdditionalSamples` 排序去重并排除原七点。去重后的每原单元完整点数为 7/19/61；模板只创建一次，原 k=0 点沿旧路径求值，随后流式追加加密点。

原边上的加密点仍只由 ReferenceEdge.Owner 发出，边身份使用 kind=3、原 EdgeKey 和沿小编号至大编号的有理位置；内部点使用 kind=4、原三角形编号和三个整数坐标。kind≥3 时 SampleHash 额外编码有理坐标，k=0 不增加编码且保持原哈希。层间旧点以相同有理坐标求值，集合嵌套；遍历序号随层级改变，不能把 SampleOrdinal 当作跨层位置 ID。

没有全域样本数组或跨层索引缓存。每层独立检查几何、构建查询/归属并完整求值，归约没有读取前层最大值来掩盖漏采样；局部模板生成最多只有两次四分的短数组。边界 UV/参考求值仍独立于被测网格。

## 5. 实际构建与探索入口

默认无参数运行解析夹具。手动入口为：

```text
parallel_roam_MeshQualityEvaluator_tests.exe --calibrate-reference <scenarios.csv> <camera-samples.csv> <new-output> <test129|peking547> <dod|classic>
parallel_roam_MeshQualityEvaluator_tests.exe --calibrate-bilinear-source <scenarios.csv> <camera-samples.csv> <new-output>
parallel_roam_MeshQualityEvaluator_tests.exe --check-bilinear
parallel_roam_MeshQualityEvaluator_tests.exe --check-refined-sampling
parallel_roam_MeshQualityEvaluator_tests.exe --calibrate-refinement <scenarios.csv> <camera-samples.csv> <new-output> <test129|peking547>
```

`Calibrate` 拒绝已有目录和未冻结名称，复用 manifest 校验与 `BuildCameraView`。C++ loader 不验证资产文件 SHA-256；本次运行前已由外部身份采集核对，见产物 `input-identities.json`。

当前 `--calibrate-bilinear-source` 固定 test129/DOD/sample5，不能通过参数展开其他自然项。`Calibrate` 的 `bilinearSource` 参数默认 false，供旧三角入口保留历史行为；true 时 `LoadReferenceSamples` 调用既有 stb 的 `stbi_load_16(...,1)`，以带 `stbi_image_free` 删除器的 unique_ptr 管理解码缓冲，校验与 HeightMap 的宽高一致后复制到驱动拥有的 uint16 vector。8-bit 扩展 ×257、行优先且不翻转；double 归一化在评价器内执行。

`reference.csv` 的 primaryReference/protocol/decoder/normalization 冻结参考身份，buildMs 记录规则 UV 模板构建，decodeMs 单列额外原始样本解码。双线性模式的 diagonal 仅标识采样模板，不是主参考曲面剖分。源向量与两侧网格存活到同步求值结束，没有新的共享缓存或生产接口。

自然设置固定：test129 d=14/B=32768，Peking d=20/B=2097152；split/merge=-1，局部约束开启，串行各 worker=1，topology/pass 诊断 true，pair false。各进程新建一个真实构建器并只 Build 一次。参考规则 Build 与 ROAM Build 分别计时。

`DepthHistogram` 用 float UV 转 double 计算面积；`frexp(twiceArea)` 必须为精确 0.5 尾数且 depth=1-exponent 在 0..20。所有叶均在目标深度且叶数等于完整树数量、Stats 数量/最大深度相符才标 complete。DOD 另比对 `ActiveLeafNodes` 的真实 DepthAt 直方图，Classic 不开放私有节点。

`SaveBuild` 输出 depths.csv 和 build.csv，后者包含 full-depth 判定、拓扑/队列诊断、节点/叶/预算、split/forced/merge/拒绝、mesh hash 及构建/诊断成本。内部停止细节没有公开时记录 `stop_detail_unavailable`。

quality.csv 每行保存参考相机身份、实际层级/样本/异常/最大位置/成本，RMS 显式 `not_implemented`。status.txt 区分完整深度输出和当前可达细化残差，并记录请求层级与 `midpoint-fourway-rational24-v1` 采样协议。完整细化与 `sampled_only` 仍不自动决定参考适用性；质量量级由人工检查点解释，不嵌入事后像素接受阈值。

status.txt 另标 `pointwiseExcess=not_paired_not_implemented`。`--check-bilinear` 保留既有双线性/查询/投影夹具。新 `--check-refined-sampling` 执行层级计数、旧 k=0 身份、固定尖峰、重排/覆盖/资源检查及受影响的双线性/投影负例，输出主要类型尺寸供内存预检；两者均不调用 `CheckRealRefinement`。默认无参数入口仍含全部本目标解析与已有细化检查，本轮未运行默认入口。

LEGACY 三角参考模式保留 Peking 第二相机逻辑；双线性模式只运行该资产第一相机。新 `--calibrate-refinement` 固定 DOD：test129 为 sample5 的 k=1/2，Peking 为 sample9 的 k=0。`Calibrate` 用借用 span 接收请求层级，每个进程只 Build 一次；test129 先比对原冻结网格/归一化哈希，指定运行还要求完整细化和拓扑有效。某层异常则停止剩余层并以失败退出。全部层共享 600 秒评价时间配额，外部监测补充不可抢占阶段保护。

## 6. 已观测证据与未实现边界

LEGACY 首轮仅运行 test129/DOD/sample5，产物为 `benchmark-output/ordering-relaxation/gate-02-20260911/calibration/test129-dod/`。实际 32,768 叶全部深度 14，65,534 节点；原诊断无违规，零拒绝/合并。k=0 有 98,817 个域样本、98,236 个参考视锥内样本，无覆盖/投影异常。

observed screen max 为 0.65399166495088779 px，observed height max 为 0.015686333179473877 世界单位。屏幕最大点对应原格子 (47,123) 中心，原像素值 `[85,82,85,80]`；固定与另一条对角线的中点高度分别为 1.3098039216 与 1.2941176471，吻合实际两位置。独立相机公式重算为 0.6539917306 px。

INFERENCE：该点的残差可由两种剖分的表示差异解释，不需要排序松弛或不完整细化。不能据此推广所有地形/Classic 的残差，也不能将该标量从其他网格最大误差中相减。

已按小规划停止：自然 Classic、Peking、k=1/2/3、RMS 和正式质量比较均未运行/实现，不填零。默认解析用例中运行的两个 depth-4 builder 只验证受控设置和几何出口，不属于自然校准矩阵。

FACT：随后经用户确认，仅执行双线性 source 的同一自然项，产物 `benchmark-output/ordering-relaxation/gate-02-bilinear-20260911/`。构建语义/网格哈希/逐深度直方图和 98,817 样本身份与首轮一致，重新计算参考域后屏幕样本仍为 98,236，评价异常为零。

FACT：新 `sampledScreenMaxPx=0.32850898581588572`，序号 92749，UV=(0.12109375,0.94140625)，cell=(15,120) 中心；四角 `[60,59,61,58]`，source y=0.93333333333333335，measured y=0.94117647409439087。独立相机公式为 0.32850901947640887 px。`sampledHeightMax=0.0078432555292167194`，位于 cell=(60,34) 中心。与旧参考最大点不同，不能声称全局残差精确减半。

INFERENCE：这些点支持有限三角表示对双线性 cell 的逼近残差解释，并保留 float/double 数值差异；没有连续极值或采样收敛保证。评价 56.3811 ms、峰值 34,906,112 bytes 是单次离线成本，不能作统计收益结论。生产源码/链接未变，probe SHA 与首轮相同，复用原 10 项工程证据，没有重跑生产性能矩阵。

## 7. 采样加密与非二次幂输入的新增证据

FACT：用户授权的两个自然进程已完成，位于 `benchmark-output/ordering-relaxation/gate-02-refinement-20260911/`。test129 k=1/2 的样本数分别 394,241/1,574,913，屏幕样本 391,981/1,566,021；前轮 k=0 被引用而非重跑。三层屏幕最大值 0.32850898581588572 px、高度最大值 0.0078432555292167194 及两种最大位置均相同，Δscreen/Δheight 两次都是 0，满足冻结经验稳定条件，不是连续最大值证明。

FACT：Peking 是 547×547 的 16-bit 单灰度 PNG。一次 Build 生成 2,097,152 个深度 20 叶、4,194,302 节点，预算利用率 1，实际深度双重核对一致；原诊断无违规。sample9/k=0 有 1,790,881 个域点、1,781,177 个屏幕点，评价异常零。屏幕最大值 0.036423925632701193 px，高度最大值 0.0052994334150855416。

FACT：Peking 屏幕最大点 UV=(0.28754580020904541,0.68681317567825317)，靠近原格点 (157,375)，实际 cell=(157,374)。直接解析原 16-bit PNG 四角 `[6096,6175,6213,6292]`，source y=1.1376516062980331、measured y=1.1340980978784501；独立相机重算 0.03642392133086831 px。最大高度点接近格点 (446,11)。核算使用实际 float UV，不吸附回原整数格点。

INFERENCE：本项非对齐输入未显示 source/查询/投影异常，支持继续使用已批准 reference；Peking 仍无加密收敛证据，不能推广其他输入或根据跨资产像素大小比较算法优劣。

FACT：test129 k=1/2 评价 129.2765/347.9625 ms，Peking 评价 6916.5949 ms、Build 17116.1734 ms、进程 24.5243 秒、峰值约 1.63 GiB。没有超原成本上限；每层成本包含独立索引，生产路径未变，复用先前工程证据。单次成本/采样量变化不是统计版本收益或回归结论。

## Unresolved / Uncertain

当前有限采样之外的连续域最大值、Peking 高层收敛、其他自然输入、RMS 和 relaxed/serial 配对退化尚无本轮证据。未发现需要标记为 UNCERTAIN 的关键所有权问题。
