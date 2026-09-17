# QPC-06A 精简证据

原始平台、提案和命令位于忽略目录`benchmark-output/cpu-refinement/qpc-06a/run-01/`；P0/A1/P1分别是基线、仅局部证据复用、再加binary64直接累计（含低开销构造计数）。

`summary.json`由`scripts/analyze_transactional_quality_cost.py --reduction-input benchmark-output/cpu-refinement/qpc-06a/run-01 --output docs/research/cpu_refinement/data/qpc_06a_quality_cost_reduction --arms P0 A1 P1`生成，包含原始CSV/JSON SHA、各事件时间、28见证内部三次认证及程序身份。`diagnosticWork`是诊断完整轨迹的物理工作计数/任务时间，不能与正常平台时间相加；`supplement`保存L政策参照及Sierra一次定向复测。

复现采集见`scripts/run_transactional_quality_cost_reduction.py`；原04F/G输入只读，完整资产/相机/种子身份在runner生成的各manifest及inputs下。正常模式关闭详细诊断、perf、Tracy；微测和提案审计另跑，不改变生产决策。三次内部重复不是独立进程。
