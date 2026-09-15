# PQ-02：可选残余见证与局部几何记录的代码事实

2026-09-15。范围为[小规划](../../plans/cpu_refinement/pq_02_residual_provenance_plan.md)的新增诊断及其归约；原两见证、恢复链与高度策略沿用 [PQ-01 事实](pq_01_quality_provenance_facts.md)。实测见[结果](../../research/cpu_refinement/pq_02_residual_provenance_results.md)。FACT 为源码事实，INFERENCE 为推断。

## 1. 文件、归属与依赖

| 文件 | 本轮实际变化 |
|---|---|
| `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h/.cpp` | Extend：构造接受可选一个 `Point`；原两点保留；新点覆盖的已批准补丁追加只读几何与样本证据 |
| `tests/TransactionalQualityProvenanceProbe.cpp` | Extend：`OUTPUT [immutable] [--witness U V]`；输入验证先于输出目录、资产和种子创建 |
| `scripts/run_transactional_residual_audit.py` | Create/Wrap：复用 `native_run`、`identity`、`CORE_FIELDS`、`compare`；保存版本与见证身份、回放对照、归约、同点独立误差和局部角度推算；可选复用 EIP 绘图字体 |

FACT：生产 `TransactionalProposals`、`TransactionalCertification`、`TransactionalPredicates`、`TransactionalReservation`、`TransactionalPipeline`、公共适配、应用、渲染器及 CMake 均未修改。依赖仍为独立探针→实验诊断→只读核心；核心没有依赖实验层。

FACT：新增代码只被既有追溯目标链接。新的绘图依赖只有指定 `--figure` 时才导入；正常归约和原生运行不依赖 matplotlib。没有新增采样器、质量评价器、进程控制器或拓扑求解器。

## 2. 对象、所有权与时序

`TransactionalQualityProvenance` 仍由探针栈对象持有，寿命覆盖24批。

| 状态 | 所有权 / 生命周期 |
|---|---|
| `_points` | 自有 vector；先复制固定两点，再追加有限单位参数域坐标，构造后不变，最多3点 |
| `_expected` | 自有 vector，与 `_points` 等长；批前写预测，批后比对内部曲面 |
| `_future/_returned` | 自有配置副本；只供跨视图投影，不反馈核心 |
| `_survivors` | 原B诊断的旧高度表；规则和生命周期未变 |
| 三个文件流 | 对象自有，原17位精度和异常规则保持 |
| `WritePatch` 中 `unfitted` | 一个局部提案副本；仅复原新点初高，记录后销毁，不进入认证、批次或发布 |
| `closed` | 局部样本槽集合；去重全部支持面的关联样本，只用于与可见认证集分开计数 |

FACT：`Before` 在原 `Plan` 返回后、`Apply` 前同步运行，`After` 在应用和网格消费后运行；不会与核心写入并发。参数借用 const 状态/样本/提案，不跨线程保存引用。生产工作账本不累加诊断成本。

实际控制流：

~~~text
CLI Coordinate 验证 → 输出目录 → 原公共种子 / 核心初始化
→ 原24批 SetView / Plan
→ Before：原见证与可选见证观测、恢复链、已批准事务记录
   → 仅追加点被提案覆盖时 WritePatch
→ 原 Apply / ConsumeMesh
→ After：预测比对 / B存活高度核查
→ 原帧记录及五份实际float网格
~~~

## 3. 几何记录的含义

FACT：`WritePatch` 的输入是同一批前状态及已认证的提案，不重新选择事务。记录：

- `oldFaces`：支持面逻辑身份与各顶点的身份、UV、高度。
- `newFaces`：提案最终连接；`points` 仍由原记录保存实际拟合几何。
- `pointReferences`：各提案点的原始高度样本双线性值。
- `removedPoint`：仅回收提案记录被删除中心的几何与源值；接收为 null。
- `newPoint`：接收提案的新点身份、旧根面插值初高、拟合高、源高；回收为 null。
- `unfittedWitnessHeight`：把新点高复原为旧根插值后的局部曲面值；其他提案点保持记录值。它不是一般“取消全部拟合”的反事实，尤其不能用于取消A的旧点高度调整。
- `closedSampleCount`：支持面所有关联样本去重数量；不等于连续曲面的严格覆盖证明。
- `oldVisibleMaxPx`：原提案可见样本的旧平方误差最大值开方。
- `nearestVisibleSample`：到新增见证参数距离最小的可见认证样本；只是诊断，不改变归属或采样。

FACT：新点初高使用现有 `CurrentHeight`，与 `TransactionalProposals::Prepare` 相同的根面顶点、重心权重和运算括号。实测两次 E 的未拟合见证值都与旧曲面完全相等。

原样保持：`Owner` 全域定位且按逻辑面身份打破覆盖并列；`Rank` 使用生产 `PriorityKey`；原 `Recovery` 只解释覆盖根的处理，不能证明其他邻近根都不能修复。所有实际批准提案仍逐个判断见证覆盖，避免仅看根而漏掉邻域修改。

## 4. 脚本、身份和失效规则

FACT：

- `baseline` 必须使用新输出目录；复制修改前程序与三份C++来源，冻结旧质量JSON身份和完整UV，执行原B两见证回放。
- `collect` 检查质量来源身份；保存/核对修改后程序与三份C++来源，再运行默认和追加点。沿用已有原生命令缓存、180秒和8GiB限制；修改来源后不得复用同一个运行目录。
- `report` 要求原三个默认诊断文件逐字节一致、三次回放各24帧 `CORE_FIELDS` 一致、五份实际网格SHA一致；要求追加点有48条观察且坐标等于冻结值。
- `point_quality` 检查冻结质量JSON、相同采样哈希与数量、误差数组字节数和序号范围；以既有little-endian f64格式读取同序号两误差，拒绝非有限值，并记录四个输入文件身份。
- `root_shape` 只在最后一次改变点的已记录新面中寻找唯一严格包含面；用 `Fraction` 保留输入double的精确有理值，复算锐角判据和三条中点边细分的根側子面。不加载全域、不构造邻面、不搜索新修复。
- `figure` 读归约数据；高度和固定未来投影用批后值、排名用批前值。当前不可见误差设为 NaN，不连接成“可见质量”曲线；输出PNG/SVG。

`root_shape` 是本次冻结残余报告的有限推导，要求严格内点；未来若选择边界见证或不同轨迹，应调整诊断契约而不是静默选面。图中的不可见区间也对应本轮已冻结的可见性，不是任意相机协议模板。

## 5. 本轮依赖的生产规则

FACT：`Fit` 先逐新面检查 `Shape`，然后要求可见样本非空；目标为旧见证精确误差向下取整再减10000微像素。一维选择可行增量区间中最接近零的值，最后按实际发布高度执行 `Measure/Accepts`。

FACT：E 在内部边中点插入，并同时二分两侧面；F 在面内见证或重心插入；H 在重心插入且保留邻域，B不允许调整旧点高度。`Shape` 只读U/V，要求正方向，并用精确锐角平方比较保证最小角 `acos(sqrt(9/10))`。

INFERENCE：记录的根最小角已经等于该下限，F/H向严格内部插点会分割该角，因此当前扇形连接不能合法继续；两条E边的根侧子面已经违反判据。第三条E根侧合法但整提案失败，失败必在相邻面侧。最后一项由实测目录结果和源码控制流共同推出，没有记录相邻坏角的精确身份。

## 6. 成本、验证与风险

INFERENCE：既有 `Owner/Rank` 对每个见证具有 O(N) 扫描成本；B旧高度检查亦为全域诊断。新增点将见证数从2增到3，没有改变生产复杂度。

`WritePatch` 包含局部提案拷贝、支持点/面记录及样本集合构造；若关联样本访问总数为s，可保守计 O(s log s) 去重，另有可见样本线性遍历。几何支持有界不能把这部分样本工作称为O(1)。归约哈希检查读取完整导出文件，绘图和有理数局部推算均为离线费用。

FACT：定向原生构建完成；默认诊断文件不变；三次轨迹和网格对齐；非法数值输入在目录创建前拒绝。原两见证进程4.412008→4.434349s，+0.51%；追加点5.226964s，额外诊断费用单列。没有跑全部CTest或重复平台质量/性能矩阵。

风险：`WritePatch` 初高公式与生产 `Prepare` 必须同步，后续修改初高规则需要更新诊断；反事实不能被用于认证后改高；追加字段不能污染正常工作账本；离屏投影不能解释成当前可见误差；有限目录失败不能扩大成所有恢复操作无解。

## 7. 规划对照与符号索引

计划中的可选点、局部几何、默认兼容、轨迹身份、有限费用对照均已实现。归约补充同点DOD旧评价读取、根侧角度推算和可选时序图，属于规划允许的局部只读解释，未增加自然运行或算法政策。

- `TransactionalQualityProvenance::{Before,After,Observe}`：实验追溯cpp；控制只读观察、预测与结果核查。
- `WritePatch`：同cpp匿名命名空间；记录被新增见证覆盖的实际提案，不被生产调用。
- `Coordinate` / `main`：追溯探针；输入验证、固定回放和输出。
- `baseline/collect/report`：新增脚本；版本冻结、有限采集和来源归约。
- `point_quality/root_shape/figure`：同脚本；分别为旧独立证据读取、局部精确角度、可选诊断图。

## Unresolved / Uncertain

本轮没有证明受限新点高度或固定源高度能保持当前认证可行性；没有证明更换目录后存在快速恢复，也没有建立持续质量保证。

未还原第三条E相邻侧的具体坏角；当前信息足以定位到相邻面形状失败，不据此猜测更广依赖范围。未审计所有潜在邻近根修复，也没有评价修改质量政策后的性能。

当前扫描没有发现本次新增对象关键所有权或销毁顺序不明确事项。
