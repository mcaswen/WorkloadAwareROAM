# QPC-04F 质量成本调查数据

对应[性能问题分析](../../../../reviews/cpu_refinement/qpc_04f_quality_cost_analysis.md)。这是原04F数据再归约，不是新采样矩阵。

- `summary.json`：原始输入SHA256、共同配置/种子及216个B机会的正常/诊断离散核对；正常墙钟、事件、工作、诊断子计时、静止空批、脚本指纹与归约耗时。
- `frames.csv`：B逐机会正常平台时间与独立诊断账本并列表。字段`*Ms`是正常平台ms；`receiver_stage`、`task_wall_sum_*`及`pointwise_batch`是诊断秒。不能直接相加。
- `quality-cost.png`：正常平台暖成本与时序图，已实际目视核对；不绘制混用两进程计时的伪分解。

在项目根目录从Ubuntu执行：

```bash
python3 scripts/analyze_transactional_quality_cost.py
/mnt/c/Users/mcaswen/miniconda3/envs/mesh_splatting/python.exe scripts/analyze_transactional_quality_cost.py --plot
```

归约只需Python标准库；制图复用已有实验绘图组件及已安装matplotlib的解释器。大原始输入仍位于忽略目录`benchmark-output/cpu-refinement/qpc-04f/run-01/`，在其他机器复现需提供该原始目录；本目录不是完整原始数据的替代品。脚本不启动基准、修改算法或改变冻结政策。
