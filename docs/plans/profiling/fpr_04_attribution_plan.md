# FPR-04：家族窗口、实际归因和使用契约

2026-09-14；承接 `51c7521`，前三阶段原始数据与审查继续有效。遵循[大规划](cpu_function_profiling_major_plan.md)，本阶段结束 profiler 工作，不自动启动 GWR。

## 文件归属与实现

- Extend `GreedyTransactionalLodFamilyProbe.cpp` / tests/CMakeLists：链接同一个 ProfileSession；新增可选 `--profile 1`，保留原十四帧引导、八个报告帧及最终离线质量评价。只在报告帧包围 BuildRenderData；不复制控制器，不改家族设置。首版家族采集不支持重复，避免为了 profiler 复制整条 bootstrap 控制流。
- Extend `run_cpu_profile.py`：在同入口增加 classic/dod 模式，家族只允许一个重放；仍使用已有后端和窗口报告。扩展 `tracy_report.py` 的家族阶段/池线程说明，不将 GTP 任务标签假设强加给所有家族。
- Extend DOD Pipeline/PassExecution、Classic MeshBuilder/适配入口：少量静态构建/阶段作用域；五阶段不改顺序或接口，无 GPU 内容。
- Create 文档：使用指南、最终热点→工作削减建议表、当前代码事实、阶段审查；链接真实 trace 和采样报告，明确等待/CPU/self/inclusive 的区别。

## 验证与成本

实现前已保存普通家族可执行文件并运行 test129/DOD 八轮作为基线。修改后仅同输入关闭、未连接、Tracy 三档，结果核对最终 mesh/质量及逐轮逻辑字段。Classic 通过共同目标编译与一个短轨迹入口检查，不扩自然矩阵。两场景 GTP 时序和 perf 完整复用；无需重录以更新提交号。

至少一次实际 perf 官方热点指令/源位置定位，保留原程序与构建身份；不能对重编译后不同二进制无声套用旧采样。家族短任务样本少，不追加 perf 重放追求热点表；本轮家族验证以窗口与静态阶段时间线为目标。

检查普通目标无 Tracy 符号、原性能没有明显回归；继承 FPR-03 有效关闭证据。脚本新增 CLI 限制与源位置导出只做必要验证，不重跑所有工具故障测试。产物仍忽略，提交解释性数据与复现命令。

## 出口

可直接运行的 perf/Tracy 入口、正式工具产物、明确数据质量与扰动限制、两场景函数及线程归因、后续 GWR 优先级建议。归因不能扩大成原因已证明或预支算法速度；新增优化仍等待后续任务。

## 实现情况

已完成一个 DOD 八轮捕获（437 个区间）、Classic 短轨迹入口、关闭/未连接/捕获对照和结果比对。官方 annotate 对 Project 的 554 个样本输出指令，addr2line 与哈希匹配旧源码恢复源位置。最终审计补上实际输入依赖归档，早期补档明确注明为事后工作。见[最终报告](../../research/profiling/cpu_function_profiling_findings.md)、[使用指南](../../research/profiling/cpu_profiling_usage.md)与[审查](../../reviews/profiling/fpr_04_attribution_review.md)。GWR 未实施。
