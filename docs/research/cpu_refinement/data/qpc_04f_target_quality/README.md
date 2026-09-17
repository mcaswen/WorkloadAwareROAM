# QPC-04F 精简证据

- freeze.json：运行前冻结的三场景、目标、来源、见证与审计批次。
- sources.json：A/B正常程序、进程边界、种子身份和原清单摘要。
- frames.csv：A/B全部机会的正常计时、实际面数、请求与执行账本；未删除冷启动或零事务尾帧。
- summary.json：六批独立有理核查、双表示域进展、12帧独立质量、DOD逐点比较、工作/成本与停滞。
- known-counterexamples.json：旧坏补丁在新条件下的只读复核；violation不表示新政策接受违规。
- witness-summary.json：原固定见证的seed/关键帧/末帧高度、固定未来视图及完整尝试原因。
- verification.json：定向检查与注释统计；未宣称完整C++机械证明。
- visual-evidence.json及图片：实际平台图来源、共享色标热图和同一最大见证局部裁剪。

完整产物保留在Git忽略的benchmark-output/cpu-refinement/qpc-04f/run-01/，包括最初schema拒绝、旧观察器误报、构建日志、正常/视觉/诊断清单、网格、完整误差数组与捕获几何。原始大文件不加入Git；本目录保留可审阅摘要、图及输入身份。

采集入口：scripts/run_transactional_target_quality.py；分析入口：scripts/analyze_transactional_target_quality.py。两者通过--output指定上述原始目录，分析再以--target指定本目录；--visuals-only只重建图。采集需要冻结Windows app/probe与已有独立质量probe，通过Ubuntu WSL调用。Python使用已有roam-experiments虚拟环境。

正常计时二进制在sources.json记录；后续只读观察器修正、排版和测试不覆盖正常运行身份。analysis会复用04D/04E原始质量与坏补丁，不能脱离这些来源仅凭本摘要重新算质量。完整Q_eval均为k=0 sampled结果；RMS为可见地形域样本等权。运行时证书的Q与Q_eval坐标表示不同，不能混称连续误差保证。

质量账本存在包含与重复：pointwise_quality是任务墙钟和；Reasons的配对拒绝可重复；证据字节为累计估计负载而非存活峰值。全轨迹逐交换精确进展幅度没有导出，只有预定批次的独立幅度。
