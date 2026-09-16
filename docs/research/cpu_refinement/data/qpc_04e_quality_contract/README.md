# QPC-04E 聚合证据

对应[结果页](../../qpc_04e_quality_contract_results.md)。本目录只保留可审阅聚合、图和来源指纹；原始局部几何、U16、逐点表、程序副本和日志在忽略目录`benchmark-output/cpu-refinement/qpc-04e/run-01`。

- `freeze.json`：采集前四轨迹、观察帧、六个操作点、二进制/源文件及配额。
- `collection.json`：四条完整原轨迹与历史公共输出一致记录。
- `summary.json`：71个完整交换、两侧判定、精确进展界、工作量、请求缺口及默认计时。
- `retention.csv / request-gaps.csv / work.csv`：同一summary的展开表，不能独立修改统计口径。
- `lean-verification.json`：实际工具、命令、15条抽象引理及五个主声明的内核依赖。
- `verification.json`：源指纹、逻辑测试、性能首轮/复测、资源与视觉核查记录。
- 两张PNG：旧补丁局部几何/误差图、固定参数的交换保留表；不是新政策连续输出。

数值引擎补充了存活几何核对及精确和界字符串后，仅重算已有局部JSON，没有重新运行自然轨迹。`freeze.json`记录采集时脚本身份，最终数值身份以`summary.json.checker`与`verification.json.analysisChecker`为准。首版归约另存原始目录的`analysis-initial`，未删除。

复现时从项目根在Ubuntu运行：

```bash
python3 -m unittest discover -s tests -p test_exchange_quality.py -v
tools/downloads/lean/lean-4.8.0-windows/bin/lean.exe docs/research/cpu_refinement/formal/quality_contract/QualityContract.lean
python3 scripts/run_transactional_exchange_quality.py --output benchmark-output/cpu-refinement/qpc-04e/run-01 --probe build/relwithdebinfo-d3d12-fetch/tests/RelWithDebInfo/parallel_roam_experiment_cpu.exe
python3 scripts/analyze_transactional_exchange_quality.py --output benchmark-output/cpu-refinement/qpc-04e/run-01 --figures
```

采集器拒绝冻结身份变化和输出覆盖；全新复现先保存当前旧探针与`baseline.json`，按规划执行normal-before。制图使用基础设施固定依赖，已有Ubuntu环境为`/home/mcaswen/.cache/roam-experiments/venv/bin/python`。Windows原生程序/Lean经WSL调用，不能把本轮计时称为Linux原生性能。
