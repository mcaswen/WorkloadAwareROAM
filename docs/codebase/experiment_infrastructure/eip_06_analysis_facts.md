# EIP-06 计量与质量事实

2026-09-15；[规划](../../plans/experiment_infrastructure/eip_06_analysis_plan.md)

result_adapters按eip-run-v1的60列和manifest SHA读取；TPI、SVE、perf与Tracy分别指定协议，历史不并入本次总体。analysis是唯一统计归约，保存所有机会、空值、冷启动/预热/运动/恢复分组。独立进程是重复单位，开发单进程不计算CI或P95。OpenGL未实现的GPU等待值转null，CPU回放没有图形列；阶段不相加为父级两遍，未归属成本单列。

质量仍调用MeshQualityEvaluator的raw U16双线性参考。可选QualityPoint observer只暴露原求值点/参考clip/高度与屏幕误差，不改变采样或返回数值。QualityPointRecorder保存约5万序号均匀点和全Q实际最大见证；热图是稀疏显示，PointErrors仍完整。现有probe默认命令保持，--locations仅增加离线CSV。RMS明确是可见参数域样本等权，不是screen-space weighted。

旧/新关闭输出在固定山脊mesh0上所有非时间字段完全相同：256.169/258.136ms；开启点记录657.550ms，新增约0.4秒离线I/O，不放进运行帧。新增observer解析夹具和原bilinear失败/几何夹具通过；Python五项测试核对缺列、重复列、null、内容篡改、恢复分类。

Peking DOD/immutable各4个关键帧、山脊2个关键帧完成独立评价。Peking frame15 DOD Emax=0.4187275px、immutable=2.3510500px；返回Emax=0.4418856px，Dmax仍约0.1011252px。保留残余，不授予同质量性能通过。逐点Dmax按同源/同采样/同视图核对，未用两个Emax相减替代。已查看实际frame15与C++参考投影见证叠图，参考UV与屏幕方向对应；稀疏显示条纹是序号抽样，不是网格裂缝。

新CPU入口复用perf FIFO会话采集24个真实窗口，1,124 ROI样本，无缺栈/未知栈顶，完整符号174项；暖窗口仅739样本，不足既有2,000样本热点筛查标准，标“入口验证/样本不足”，不追加长跑。Tracy复用FPR-03既有Peking捕获，原8窗口和区间层级解析通过；它是GWR之前20k/4线程历史程序，不能拿其时序归因当前Windows32ms。

原始证据位于benchmark-output/experiment-infrastructure/eip-06；首次9运行/历史附件归约6.73秒（含SHA校验），未运行算法。quality-index、dmax和analysis可追溯实际mesh。完整图表/报告在下一阶段生成。
