# FPR-02：自然更新窗口与函数采样

2026-09-14；小规划，承接[大规划](cpu_function_profiling_major_plan.md)与[能力事实](../../codebase/profiling/fpr_01_capability_facts.md)，遵循[开发规范](../../standards/development_guidelines.md)。FPR-01 提交 `081c90f`，用户已允许小规划自主闭环。

## 目标与职责

在现有 GTP 八轮计时循环取得两场景 C4 的实际函数自身/调用路径热点。当前工具组合采用 FPR-01 已验证 FP 构建；DWARF 不作为已解决能力。正常执行、初始化和诊断仍分开；不改变算法或统计人口。

| 处理 | 文件 | 职责 |
| --- | --- | --- |
| Create | `src/tools/profiling/ProfileSession.h/.cpp` | 探针独占的控制/应答会话和单调时钟窗口；不依赖 GTP、不进入生产算法 |
| Extend | `tests/GreedyTransactionalLodProbe.cpp` | 保留旧 CLI；显式 `--profile <replays>` 仅允许 trajectory timing，重建同一输入的状态后运行相同八轮 |
| Extend | tests/CMakeLists、解析夹具 | 会话仅链接实验/工具，解析夹具提供真实控制握手的有限验证入口 |
| Create | `scripts/profiling/perf_backend.py` | 受控 perf、原生临时目录、时间域与栈导出；复用已存在环境和进程清理能力 |
| Create | `scripts/profiling/report.py` | 全样本分母、自身/含子调用聚合、ROI 外样本及未知比例；不假定所有栈都解析成功 |
| Extract / Reuse | `scripts/profiling/process.py`、既有 file_identity | 两类采集复用自有进程组清理，避免自然后端依赖解析夹具模块；文件身份直接复用既有工具 |
| Create | `scripts/run_cpu_profile.py` | 单次自然采集入口、源码/程序/输入身份、产物归档和预算；暂只开放 perf，后续扩展 Tracy |
| Extend | Python 定向测试 | 采样窗口过滤、缺栈分母、控制超时/缺失以及输出保护 |

`probe → ProfileSession → perf FIFO`；`runner → perf_backend → 官方 perf`；`report → 官方导出和窗口文件`。算法不依赖 profiler 会话。沿用现有计时/JSON 路径，重复仅新增外层状态重建，不复制另一套 Update 控制器。

## 窗口与采集契约

perf 用 `--delay=-1 --control=fifo:... --clockid mono`；会话在更新前 enable/ack 后读取 CLOCK_MONOTONIC，在更新后先记录结束再 disable/ack。文件输出位于窗口外。控制耗时另列，报告再按精确窗口过滤握手边缘样本；异常/无 ack 有限失败并保留不完整证据。

输入仍为 GMP 冻结 test129/Peking sample14，C4 原八视图。初采各一个进程一次轨迹；若有效样本不足 2000，依据初次样本数冻结一次补采，最多 32 次同种子重放或 180 秒。初始化、输出和状态析构在窗口外；重放用于采样充分性，不是独立实验重复，也不把后续收敛帧重复当更忙输入。

保留 `perf.data`、官方 script/report、完整 argv、可执行文件和调试身份、进程结果、每帧窗口、每帧业务摘要、unknown/lost/截断记录。所有大文件写 Linux 缓存再归档，单文件 512MiB，总时限 180 秒。发现超过上限时结果标记不完整而非裁剪为成功。

## 快验与出口

复用 FPR-01 普通 C4 test129 后测作为修改前基线；本次重编译后按同输入八轮取得普通对照。记录均值/中位数/最近秩 P95 与逐轮逻辑和实际 mesh，超工程门槛才至多一次定向复测；不跑家族/图形矩阵。

控制通道解析夹具只核验必要风险，采样报告必须实际看到业务函数和子线程。自然 profiling 与对应普通路径比较意图、交换、最终 mesh、后续轮次状态，不允许借 profile 模式打开全量诊断。符号/FP 构建的无采集开销与普通构建分别记录。

完成后输出两场景函数热点报告，并冻结 FPR-03 的基础函数标记清单。热点只决定值得观察什么，不在本阶段优化 GWR。

## 实现情况

工具能力已完成，真实采样与结果核查见[采样报告](../../research/profiling/fpr_02_function_sampling_report.md)。普通探针的关闭路径短对照存在性能疑点，由已授权的 FPR-03 插桩开销核查承接；不标为无条件性能合格。

首次受控采集发现 perf 7.0.14 应答实际为 `ack\n\0`，会话已兼容结尾 NUL 与分段读取，原始失败保留。首次有效八轮 test129/Peking 分别得到 152/162 个 ROI 样本，按 `ceil(2500/初次样本数)` 冻结唯一一次补采为 17/16 次同种子重放；多出的采样余量用于减少短窗口随机不足，不增加自然场景或改变轨迹。

主函数表采用 perf `--no-inline` 的实际机器符号，避免把不同类的未限定 `size`/`operator()` 内联名错误合并。调试信息仍保留，可对实际热点再用官方 inline/annotate 定位；未声称这是所有源函数的精确调用成本。
