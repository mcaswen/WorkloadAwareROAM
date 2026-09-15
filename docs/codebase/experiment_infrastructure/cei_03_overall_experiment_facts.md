# CEI-03 正式四算法实验事实

日期：2026-09-16。关联[阶段规划](../../plans/experiment_infrastructure/cei_03_overall_experiment_plan.md)、[报告](../../research/experiment_infrastructure/fer_02_overall_results.md)。本文件记录CEI-03增量；共享模块详见[CEI-01](cei_01_shared_evidence_facts.md)与[CEI-02](cei_02_reports_and_study_facts.md)。

## 文件、职责与依赖

FACT：`scripts/experiment_infrastructure/formal_protocol.py::prepare()`生成FER-02配置，不运行原生程序、不读取实验结果。复用`catalog.resolve_case/content_hash/load_json`和`runner.save`；由模块CLI调用。

它读取FER-01协议的20个CPU case，仅将后端改为D3D12；以四资产的DOD50k配置为共同输入，新增面积16/8/4的12个CBT case。CBT固定workers=1、capacity=524288、validation=off、geometry=modified，去掉不适用的翻边和高度策略含义。

协议保存32个case哈希、相机/样本哈希、三随机区组顺序、30个视觉标记、各4个质量机会、30组逐点配对、资源限制与显示规则。随机数为局部`Random(20260916)`，不依赖全局随机状态。输出目录必须新建，已存在时失败；不覆盖冻结配置。

FACT：`comparison_figures.excess_chart()`只消费analysis中的既有qualityPairs。通过quality目录→run.path→case ID映射结果，不依赖FER文件名。每条显示有效关键帧中Dmax最大值和有效/总观察数；部分覆盖使用纹理。它没有全量重新求值或选择新样本的能力。

`quality_charts()`质量—成本图改用图外图例和不同marker，消除相邻点文本重叠。完整覆盖条件、点坐标与数值不变。共同`build()`调用`excess_chart`，因此后续报告可复用，不需要FER专用renderer。

## 控制流、所有权与持久产物

```text
formal_protocol.prepare
→ configs/experiments/formal/fer_02/{case,protocol}.json
→ formal_study.freeze/timing/visual/verify/quality/analyze
→ run manifest + inputs + sources + frames/cbt/flip-recovery
→ quality-index + pointwise pairs + analysis
→ process_statistics + report + comparison_figures + visual_report
→ 中文分析/附录 + CSV/JSON/PNG
```

runner仍是原生进程/输入/源码快照的唯一所有者。正常进程顺序执行，视觉在正常计时结束后运行，离线质量和作图不与计时竞争。GPU原生路径、shader、质量probe均未修改。

报告数据所有权属于同一analysis，FigureSpec保存其SHA256；布局修订只重绘同源12张质量图，更新PNG/SVG/PDF和报告manifest。没有重新采集或改质量有效性。

原始树：`benchmark-output/experiment-infrastructure/fer-02`，由Git忽略，保留程序、逐帧网格、完整误差数组、输入与来源。提交树：`docs/research/experiment_infrastructure/data/fer_02`和`figures/fer_02`，包含精简数表、来源身份、44张PNG与来源清单。

## 实際完成与统计边界

FACT：96个正常、30个视觉进程均正常结束，无替换。32个正常配置各3独立进程。主机为9950X3D/RTX5090D；未固定核亲和性或频率，三进程范围不是CI。

20个CPU配置通过已有实际hash/公共工作量的重复与模式核对，Transactional另核对flip-recovery；12个CBT配置只有输入/机会/代次核对，不能从空hash推出同网格。

120质量观察：CPU72有效，CBT39有效/9覆盖歧义。30组逐点配对：111个观察可配、9个不可配。无效N保留但误差值为空；最近实际N选择不因无效换参考。原始quality结果仍含失败诊断，可定位而不可用部分误差排名。

GPU计时按资源/来源代去重归属；Peking暖计算/绘制各19/21，其他各91/93，尾部不填补。资源表只使用已有分类观测，未读回尾部不自动视为无故障。关闭完整验证不等于网格合法性已证明。

## 报表字段限制与补充

FACT：共享analysis的transactionFrames由exchanges/free计数构成，未含净零翻边。FER-02另从已核对的`flip-recovery.csv`生成`connectivity-repair.csv`，保存来源SHA、attempts/executed、总事务帧和最大总批宽。未静默修改旧字段或把翻边混入预算交换分母。

`gpu-resources.csv`保存每进程已有分类观测的最大faults/dropped/activeSlots等及最小remainingSlots，不将重复异步观察相加为总工作量。正常捕获外的N仍留空，不以延迟activeSlots回填当前帧N。

独立进程/分组数据与最终质量按配置关联，尤其GPU不能解释为跨进程完全相同mesh。科学图的质量—成本点图包含启动关键帧，正文另外区分启动、转向和末帧。

## 成本与实现核查

FACT：正常流水线约1315秒、视觉475秒、质量/配对233秒、analysis74.7秒、70图报告660秒，排版重绘12图约102秒。均为离线工具总成本，不是算法帧时间。前阶段小面板工作量不同，不能据总体更慢宣称旧路径回归。

16项定向Python检查覆盖分析、统计单位与GPU观测；实际30播放器事件逻辑检查完成。70图总览及四资产真实画面对照已实际审阅；Peking/山脊标签重叠已修正。没有为文档和Python修改重复C++全构建/全CTest。

没有新线程、共享缓存或跨层反向依赖；协议生成、原生编排、统计和显示仍分离。部署无新C++接口或原生数据格式迁移。

## Unresolved / Uncertain

- UNCERTAIN：9个CBT覆盖歧义的具体几何/数值原因，本轮未更改容差或定位到根因。
- UNCERTAIN：CBI-03既有CPU工程回归原因仍开放，本轮同二进制比较不解决旧版本回归。
- UNCERTAIN：Transactional部分截图的面片明暗原因未作法线/着色归因；Emax不涵盖该视觉维度。
- FACT：真实浏览器交互和用户视觉未验证；播放器事件单测、链接检查和Agent看PNG不是用户验收。
- FACT：实验完成不能证明50k以上统一质量/性能优势；原始Peking公开再分发许可仍未确立。
