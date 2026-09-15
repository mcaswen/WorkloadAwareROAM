# EIP-06 计量、质量与函数证据小规划

2026-09-15；完成；EIP-05已闭环，按大规划自主继续

## 目标与职责

生成唯一analysis数据，供后续图表和报告消费；不改算法、参考数学或采样顺序。
- Create result_adapters.py：显式eip-run-v1/TPI/SVE/FPR适配。校验SHA、schema、frame身份；缺失为null，失败/截尾保留，禁止静默混表。
- Create analysis.py：按独立进程归约冷启动/预热/运动/静止/返回，时间互斥项和未归属时间分离。同任务线程比较须先验mesh/counters；跨算法仅成本比，质量另列。
- Extend MeshQualityEvaluator：可选QualityPoint observer，暴露实际UV、参考clip、高度误差及屏幕误差/无效域。默认空，不改数值路径。Create ExperimentQualityEvidence：限制输出约5万普通点和真正最大见证，完整PointErrors继续用于Dmax；不把降采样热图当全Q评价。
- Extend现有TransactionalPlatformQualityProbe可选--locations，默认行为保持；Python quality.py校验实际mesh hash、样本/视图身份并调用现有双线性评价器。
- Create profile_adapter.py：复用FPR perf_backend/tracy_backend、ProfileSession及已有summary解析，仅更换新case argv/ROI，不造新采集器。
- Extend公共run入口提供analyze/quality/profile子命令，文件输出独占。

## 验收与成本

1. 冻结三种模式数据归约；损坏SHA、缺列、未知schema/不匹配任务必须拒绝。独立进程是统计单位，单进程不画置信区间。
2. 对现有山脊和Peking关键帧导出质量及位置，复用真实相机矩阵；Emax/Hmax/RMS保持独立参考，Dmax只能在全Q/视图身份一致时计算。missing/NaN/-1不可合并为零。
3. 同一旧质量probe输出关闭前/后各一次，数值完全一致；位置开启新增时间/体积单列。只跑相关质量测试。
4. 新CPU ROI一次perf小捕获，复用既有Tracy捕获验证时序适配。若缺栈/样本不足明确标出，不重跑长矩阵。采集数据与Windows真实平台时间分开。
5. 输出误差UV/屏幕坐标与最坏见证供EIP07实际画图审查；本阶段以真实截图的见证范围检查为基础，不提前宣布算法质量通过。

首轮离线质量≤12个关键帧，每项120秒；之后完整验收按需补足。不扩教授级统计框架、全平台profiler或新的误差保证。

## 实施结果

质量观察默认关闭的数值完全保持，位置导出独立计费；10关键帧质量/同域Dmax与9运行归约完成。新perf24ROI、1124样本，限定为采集入口通过，暖段739样本不足热点筛查；旧Tracy保留历史身份。误差见证对实际截图已检查。[事实](../../codebase/experiment_infrastructure/eip_06_analysis_facts.md)、[审查](../../reviews/experiment_infrastructure/eip_06_analysis_review.md)。
