# FPR-01 工具能力与成本报告

2026-09-14。**能力阶段通过，采用已验证的 FP 采样变体继续；DWARF 子线程展开保留为未解决限制。** 本报告不是自然函数热点结论。

## 实际环境与工具身份

Ubuntu 26.04 / WSL2 `6.18.33.2-microsoft-standard-WSL2`，GCC 15.2。使用原有普通用户权限，没有更改 sysctl、内核或 `.wslconfig`。已有 `127.0.0.1:7897` 代理在 WSL 内连接超时，下载命令单次绕过代理，没有改系统配置。

- perf：Ubuntu `linux-perf=7.0.0-31.31`，程序报告 7.0.14；依赖 `libtraceevent1=1:1.8.7-1`，都解包到用户缓存。启动器 `/home/mcaswen/.cache/roam-profiling/bin/perf` 仅设置该工具的库路径
- Tracy：v0.14.1 / `30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c`，客户端本地 Release 编译；capture/csvexport 使用同版官方 Linux 发行二进制
- 源码归档 SHA256 `bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1`
- Linux 工具归档 SHA256 `4f57574337b206cac86758081ab21c37cf9c83fc12170d2e339e1f4ee94ff590`，与 GitHub 发行资产 digest 一致
- 构建锁定：[TracyVersion.cmake](../../../cmake/TracyVersion.cmake)；完整包/程序哈希：[tool-lock.json](../../../benchmark-output/profiling/fpr-01/tool-lock.json)

[官方发行来源](https://github.com/wolfpld/tracy/releases/tag/v0.14.1)、[源码与许可](https://github.com/wolfpld/tracy/tree/v0.14.1)。工具缓存没有进入项目源码或 Git。

## 实际能力与失败留痕

`cpu-clock:u`、`task-clock:u`、`cycles:u`、`instructions:u` 均实际打开且非零；独立短 Python 工作量只验证事件支持，不比较不同试验的 CPI。[环境原始结果](../../../benchmark-output/profiling/fpr-01/environment.json)

最初 perf 直接写项目挂载盘失败，错误 `Bad address`；改写 ext4 后同一工具可记录。DWARF 记录 189 个样本，其中主线程 63 个有完整已知调用，两个子线程各 63 个仅有事件，展开诊断报 `No such process`。没有证据将原因确定为内核、libdw 或 perf 单方缺陷，因此不修改依赖代码修猜测问题。

按已批准条件变体使用 FP 独立构建后，共 187 个样本，三个线程分别 63/62/62 个，全部包含已知 Leaf 和 Parent，原始报告 Lost=0。程序结果 checksum 与 DWARF 试验相同。FP flags 与原始 Release 分开登记，不能宣称代码生成完全无差异。[FP 结果](../../../benchmark-output/profiling/fpr-01/perf-fp/result.json)

Tracy 实际采集/退出/导出均完成：frame=1、task=2、Leaf=3、Parent=3、wait=1、exception=1、complete=1，三个业务线程。完成标志位于帧结束之后，没有负时长。采集与目标正常退出，官方导出耗时约 13ms。[Tracy 结果](../../../benchmark-output/profiling/fpr-01/tracy/result.json)、[区间导出](../../../benchmark-output/profiling/fpr-01/tracy/artifacts/zones.csv)

## 成本与默认关闭

同一现有 Release 程序运行 test129 C4 八轮；修改前后配置和决策一致。CMake 重新生成后 `ninja: no work to do`，程序 SHA 相同，默认目标没有链接客户端。

| 指标 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 八轮总时间 ms | 149.370477 | 136.927708 |
| 帧均值 ms | 18.671310 | 17.115964 |
| 帧中位数 ms | 15.747985 | 14.585690 |
| 八帧 P95 最近秩 ms | 33.081861 | 30.722087 |

这里二进制完全相同，较低耗时只能记录为本次运行差异，不能当成 profiler 优化收益。八帧不是八个独立统计单位，也不提供稳定尾部结论；没有越线退化，停止追加重复。[前后对照](../../../benchmark-output/profiling/fpr-01/performance-comparison.json)

Tracy 夹具相同开启二进制的函数工作段：未连接 126.654ms，实际捕获 128.618ms，单次约 +1.55%。采集总生命周期约 1.218s，包含连接/启动/传输，不能隐入函数耗时。关闭夹具没有 `tracy::` 符号；开启的短 CTest 约 0.21s 也表明客户端初始化不是零成本。此规模只有 12 个区间，不能外推自然插桩扰动。

## 可复现入口

在 Ubuntu 仓库目录执行，具体工具路径替换为已核验缓存位置：

```bash
python3 scripts/profiling/environment.py --perf /home/mcaswen/.cache/roam-profiling/bin/perf --exercise --output <new-environment.json>
cmake --preset cpu-profile-fp
cmake --build --preset cpu-profile-fp --target parallel_roam_cpu_profiling_tests -j 4
cmake --preset cpu-profile-tracy -DPARALLEL_ROAM_TRACY_SOURCE_DIR=/home/mcaswen/.cache/roam-profiling/tracy-0.14.1
cmake --build --preset cpu-profile-tracy --target parallel_roam_cpu_profiling_tests -j 4
PYTHONPATH=scripts python3 -m profiling.capability perf --perf /home/mcaswen/.cache/roam-profiling/bin/perf --callgraph fp --executable <fp-build>/tests/parallel_roam_cpu_profiling_tests --output <new-output>
PYTHONPATH=scripts python3 -m profiling.capability tracy --capture <tools>/tracy-capture --csvexport <tools>/tracy-csvexport --executable <tracy-build>/tests/parallel_roam_cpu_profiling_tests --output <new-output>
```

所有输出目录必须不存在；原生采集目录由脚本单独建立并在结果中登记。客户端树错误的配置已实际拒绝；仅运行两种夹具、三个 Python 用例和一组相关前后快验，没有全量 CTest 或图形构建。

后续 FPR-02 将使用已验证 FP 路径接入自然窗口，不能因为本阶段通过就声称自然热点、线程池时间线或完整双工具归因已经完成。
