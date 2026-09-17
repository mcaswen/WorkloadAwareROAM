# QPC-06B 精简证据

`legacy/summary.json`记录L0/L1；`pointwise/summary.json`记录P1/P2。两者来自显式输入归约，不覆盖历史报告。

```text
python scripts/analyze_transactional_quality_cost.py --reduction-input benchmark-output/cpu-refinement/qpc-06b/run-01 --output docs/research/cpu_refinement/data/qpc_06b_idle_plan_reuse/legacy --arms L0 L1 --allow-work-reduction
python scripts/analyze_transactional_quality_cost.py --reduction-input benchmark-output/cpu-refinement/qpc-06b/run-01 --output docs/research/cpu_refinement/data/qpc_06b_idle_plan_reuse/pointwise --arms P1 P2 --allow-work-reduction
```

基线复制自同一会话06A已有目录，原manifest/程序SHA保留；实际文件来源同时在各summary的SHA表中。`process`保留原生命令、峰值和时间。`staticSuffix`包含首个冷求解或刷新，未只摘取缓存热帧。

`zeroTouchEmptyFrames`只表示真实零样本访问且无网格写入，不等于直接导出的缓存命中数；Peking P原先已有零访问例。内部逻辑人口由缓存复用，实际工作量与计时分别保留，不能把复制失败结果当作质量恢复或新增成功事务。
