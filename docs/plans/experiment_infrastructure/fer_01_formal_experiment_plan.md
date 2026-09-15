# FER-01 首轮正式实验与分析小规划

2026-09-15。依用户“根据实验基础设施做正式实验并形成完整报告”的授权自主执行；逐阶段提交，不改变算法。形式为有限预注册实验，不是完整投稿矩阵。

## 目标与边界

比较 Classic、DOD 与 PQ-05 固定旧点、启用净零翻边恢复的 Transactional 在真实 OpenGL 平台上的成本、独立几何质量和持续工作量。以 50k 为主，同时观察 Peking 100k/200k；不把预算上限当实际负载，不把跨算法成本比当同质量加速。保留失败、零事务、慢进程和质量反例。

先复用 EIP 资产、相机、runner、analysis、quality、FigureWriter 与真实画面导出；不扩 profiler、不调算法、不新增地形或路线、不扩渲染后端。原 PQ-05 为开发试验，不混入正式重复。

## 连接修订与文件归属

| 文件 | 处理与职责 |
|---|---|
| `configs/experiments/schema/case.schema.json` | Extend：可选 `flipRecovery`，默认关闭，表达已有算法开关 |
| `src/experiment/infrastructure/ExperimentCase.{h,cpp}` | Extend：解析和验证开关，仅允许 Transactional/immutable |
| `src/benchmark/experiment/ExperimentReplay.{h,cpp}` | Extend：透传已有公共设置，不复制算法；另存现有翻边计数，不改变历史60列协议 |
| `scripts/experiment_infrastructure/catalog.py`、`runner.py` | Extend：校验、归入 task identity，防止新旧策略混淆 |
| `tests/ExperimentInfrastructureTests.cpp` | Extend：有效/不合法策略组合；复用实际配置测试 |
| `scripts/experiment_infrastructure/formal_study.py` | Create：有限冻结清单、交错独立运行和证据复用；不另造进程管理器 |
| `scripts/experiment_infrastructure/formal_report.py` | Create：从 EIP analysis 归约进程重复、图表和本次正式报告；不改变旧开发报告措辞 |
| `configs/experiments/formal/fer_01/` | Create：本轮实际 case 与预注册协议，运行前冻结 |
| `docs/research/experiment_infrastructure/fer_01_results.md` | Create：完整中文结论、方法、数据、复杂度与适用范围 |
| `docs/reviews/experiment_infrastructure/fer_01_review.md` | Create：身份、验证、视觉与实现核对 |

依赖仍为配置→回放适配→已有算法；实验收集→EIP analysis→本轮统计/报告。不会改生产默认值、事务类别或质量阈值。

## 预注册矩阵

- Peking547：冻结 `pq-return24`，24机会，预算50k、100k、200k；沿用0.25/0.10px。
- 生成山脊：冻结 `overview-orbit`，96机会，50k；沿用4/2px。
- USGS峡谷、USGS山地：冻结 `reveal-return`，各96机会，50k；沿用4/2px。
- 每个输入 Classic 1线程、DOD 8线程、Transactional 8线程；Peking50k及峡谷50k另加 Transactional 1线程。
- 共20配置，每配置3个独立正常进程，共60。每重复块按固定随机种子20260915交错次序；串行运行，不与构建、绘图或评价并发。
- 1280×720、OpenGL、关闭VSync、中性材质、depth20、增长前缀 `64B/20000`；地形世界尺度/高度比例严格取资产目录。
- 每进程前三机会单列（0为冷启动，1/2预热）；正常主指标为3..末帧的每机会CPU-ready均值。移动、返回/停留按冻结事件另列；机会时间不转换为实际定时飞行FPS。
- 进程为统计单位；主表报告3个进程均值的平均、最小/最大与全部原值。只有3次重复，不用相关帧制造置信区间或显著性结论。相同编号重复作成本比，并展示范围。帧P95只作为每进程轨迹尾延迟描述，非独立样本统计。
- 每个主配置另运行一次visual（共18），同时导出真实float网格；离线独立质量复用该输出，先逐帧核对正常/visual网格与工作计数。无需第三次重复执行同样的quality采集模式。
- Peking评价2/15/16/23，其他路线评价2/48/80/95，共72个独立全Q评价。额外截图固定stride8及入口关键帧；不根据结果追选新时间点。

## 质量与判读

Primary reference为冻结原始U16+双线性插值，k=0公共采样。记录 sampled Emax、可见参数域等权RMS、Hmax、Dmax(T,DOD)、实际N/B、覆盖及投影异常。Emax不代表连续曲面最大，RMS不是屏幕面积权重，四个时刻不能证明全轨迹质量上限。

不事后定义“明显退化”阈值。按预先列出的绝对超额预算0/0.1/0.25/0.5/1px报告相对DOD的Emax和Dmax覆盖情况；这些不是人眼阈值。主结论必须同时给出成本与质量，不能用更少修改或较低预算利用率冒充优势。无跨质量等价证据则不授予性能竞争通过。

## 实施与退出

1. 配置连接：保留旧程序，构建新入口；旧配置前后一次对照、新开关与PQ-05已知轨迹核对、配置拒绝检查。闭环后提交。
2. 冻结协议、case、二进制/源码/机器身份和次序，再执行上述60正常进程。每次180秒/8GiB；失败按截尾记录，不替换输入。全局资源风险可停止剩余运行并明确不完整。
3. 正常计时结束后采集18视觉运行、72质量评价和Dmax；核对重复/模式/线程结果。质量失败保留，不启动修复。
4. 复用EIP归约，生成进程统计CSV/JSON、PNG/SVG/PDF图、Markdown/HTML完整报告与真实图像；实际查看关键画面/图表，记录Agent视觉与用户视觉的区别。复查文档/实现后提交本实验阶段。

若结果否定性能或质量假设，实验仍可闭环；不改阈值、重复次数或样本追求正结果。实际采集限制与偏离必须在报告逐项列出。

## 实现结果

配置连接完成，定向验证与前后成本见[审查](../../reviews/experiment_infrastructure/fer_01_connection_review.md)。20个case与60项交错次序已保存至 `configs/experiments/formal/fer_01/protocol.json`。正式数据、失败记录、统计与视觉核查待本轮执行后回填。
