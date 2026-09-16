# QPC-04D 派生证据

对应[结果报告](../../qpc_04d_error_first_results.md)。协议`qpc04d-receiver-error-first-v1`，日期2026-09-16，基线`644bbf7`。A为Composite，B为ErrorFirst；与边界B原语无关。

## 文件与来源

- `summary.json`：输入/程序/参考SHA、逐场景12质量点、六档分布、Dmax、Canyon簇、实际工作、暖/冷成本、六个完整前缀审计和原见证失败链。
- `collection.json`：有限采集范围、独立质量工具身份、正常/诊断与线程兼容结论。seconds是编排耗时，不是算法帧时间。
- `tests.json`、`validation.json`：定向用例退出状态及阶段证据边界。
- `visual-sources.json`：六页图的实际截图、mesh来源与身份。
- 三张`visual-review.png`：每场景两个冻结帧的A/B实际画面与同量程误差图。
- 三张`witness-crops.png`：原问题见证与B新最大点附近的实际画面；离屏明确标记。

大体积raw数据不进入Git：`benchmark-output/cpu-refinement/qpc-04d/run-01/`保存冻结配置、正常frames/manifest、实际mesh、逐样本errors、trace和构建/用例日志。历史A来自QPC-04B或FER-02；匹配新A全部mesh hash、工作量与任务身份后才复用历史质量。DOD/off只复用原冻结参照。

## 复现入口

Ubuntu编排、Windows原生D3D12程序；从仓库根目录运行。复采必须使用新的输出目录，不能覆盖本报告原始证据。

```bash
PYTHONPATH=scripts:/home/mcaswen/.cache/roam-experiments/packages \
python3 scripts/run_transactional_receiver_ordering.py \
  --output benchmark-output/cpu-refinement/qpc-04d/run-NEW \
  --app build/relwithdebinfo-d3d12-fetch/bin/ParallelROAM.exe \
  --probe build/relwithdebinfo-d3d12-fetch/tests/RelWithDebInfo/parallel_roam_experiment_cpu.exe \
  --quality-probe /home/mcaswen/.cache/roam-profiling/build/cbi-01/tests/parallel_roam_transactional_platform_quality_probe

PYTHONPATH=scripts:/home/mcaswen/.cache/roam-experiments/packages \
python3 scripts/analyze_transactional_receiver_ordering.py report \
  --output benchmark-output/cpu-refinement/qpc-04d/run-01

PYTHONPATH=scripts:/home/mcaswen/.cache/roam-experiments/packages \
python3 scripts/analyze_transactional_receiver_ordering.py visuals \
  --output benchmark-output/cpu-refinement/qpc-04d/run-01
```

采集脚本依赖仓库已有FER/QPC历史来源和阶段默认前后基线；它是本阶段的冻结编排，不是可脱离这些输入独立发布的通用benchmark。应先阅读脚本和冻结记录，不用新构建覆盖历史程序身份。

## 解读限制

时间每配置一个独立进程，帧不是独立重复；A/B工作不同，时间比不是同任务speedup。Peking B1/B8才是同政策线程对照。质量是bilinear reference下的sampled量；六档比例可由`above / visible`直接计算。正常feasible未穷尽全部pair，不能解释为全域可行率。

自然全seed mesh hash未额外导出；夹具完整seed相同和自然见证seed相同分开记录。用户视觉验收待确认。Peking/Sierra有限正面不能抵消Canyon返回退化。
