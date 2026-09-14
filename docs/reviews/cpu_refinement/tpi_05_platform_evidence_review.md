# TPI-05 及接入大规划收尾审查

2026-09-15；对照[TPI-05规划](../../plans/cpu_refinement/tpi_05_platform_evidence_plan.md)、[事实](../../codebase/cpu_refinement/tpi_05_platform_evidence_facts.md)和[最终报告](../../research/cpu_refinement/tpi_platform_results_and_headroom.md)。

## Critical

没有未解决的平台生命周期/输出阻塞。核心独立于benchmark/GUI/render；SeedBuilder是唯一冷DOD桥，没有每帧目标副本；可变状态由适配器持有，输出借用时限清楚。OpenGL/D3D12资源恢复各有有限实测，未错误提取共同uploader。

质量限制显著但属于已知算法契约边界：Peking关键帧66px、返回31px未恢复，必须阻止“同质量优于DOD”或“持续质量已通过”的主张。报告已单列，不用数值有限/有合法mesh代替质量通过，也未静默放宽算法。

## Major

normal/export逐帧身份和B/C逻辑对照成立，性能与离线评价分离。新诊断入口没有在生产热路径增加全域扫描；帧间诊断会影响运行环境，报告已限制为有限回放。OpenGL未支持的GPU时间明确N/A，D3D12延迟GPU值不强配对。

本阶段没有扩profiler或实施新的性能算法，普通账本已能解释主要成本；未重跑无关CTest。与Legacy不是同任务/同质量配对，报告保留小输入明显慢、冷启动昂贵、Peking静止更慢等负结果。

## Minor

Artifact文件协议归实验mesh_quality，生产只在显式导出分支使用写入；evaluator/离线源解码没有进入应用链接。两个可执行入口各自职责单一，未建设通用实验平台。压力相机复用既有float公式而未链接整个SVE来源/白名单层，公式和冻结出处已登记。

首次诊断绕序错误及ExitCode问题保留原始失败记录，修正后另开run-02。没有用脚本批量改写注释或历史研究结论。新注释及整体覆盖符合规范。

## 阶段出口

TPI-01～05实现、必要验证、事实和逐阶段审查完成，按授权各阶段独立提交。大规划关闭的是**接入与可审计测量任务**；运行性能竞争力、连续质量恢复和论文竞争性并未因此关闭。后续优化/质量改动须进入新的明确规划，本次不自动扩算法范围。
