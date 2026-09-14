# CPU profiler 使用契约

2026-09-14。工具能力及限制见[本轮结论](cpu_function_profiling_findings.md)。命令在 Ubuntu / WSL 中、项目根目录执行。默认构建不开 profiler；下面是显式诊断构建。不要同时跑性能任务、编译、perf 和 Tracy 捕获。

## 当前冻结环境

- perf 7.0.14：`$HOME/.cache/roam-profiling/bin/perf`。包装器只设置本地 libtraceevent 路径，未修改系统 perf 权限。
- Tracy 0.14.1 / `30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c`。客户端、capture、csvexport 同版；锁定哈希见 `cmake/TracyVersion.cmake` 与 FPR-01 报告。
- 客户端：`$HOME/.cache/roam-profiling/tracy-0.14.1`；Linux 工具：`$HOME/.cache/roam-profiling/tracy-linux/`。
- 原始采集先写 Linux 原生缓存，结束后归档到 `benchmark-output/profiling/`。该目录被 Git 忽略；不要直接把 perf.data 的活跃写入位置放在 `/mnt/d`。

当前机器已装好本地工具。换机先做能力核查；不能仅复制工具文件就宣称事件可用。源码锁定失败、权限或工具缺失时明确失败，不自动改内核、全局 sysctl 或代理设置。

```bash
cd /mnt/d/CPP-Projects/WorkloadAwareROAM
export PATH="$HOME/.cache/roam-profiling/bin:$PATH"
python3 scripts/profiling/environment.py --perf perf --exercise \
  --output benchmark-output/profiling/manual/environment.json
```

输出路径必须新建；命令拒绝覆盖已有证据。`--exercise` 实际试开少量事件；不支持不等于零。已通过的固定环境无需每次重新试开全部事件。

## 构建

复用已冻结 Boost 1.90 头文件，保持 Release 与原严格浮点设置，不全局关闭内联。

```bash
cmake --preset cpu-profile-fp \
  -DPARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD=ON \
  -DPARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE="$HOME/.cache/roam-gtp-boost-1.90/root/usr/include"
cmake --build --preset cpu-profile-fp --target parallel_roam_transactional_lod_probe -j4

cmake --preset cpu-profile-tracy \
  -DPARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD=ON \
  -DPARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE="$HOME/.cache/roam-gtp-boost-1.90/root/usr/include" \
  -DPARALLEL_ROAM_TRACY_SOURCE_DIR="$HOME/.cache/roam-profiling/tracy-0.14.1"
cmake --build --preset cpu-profile-tracy --target \
  parallel_roam_transactional_lod_probe parallel_roam_transactional_lod_family_probe -j4
```

FP 是当前 WSL 已验证的展开路径，不是所有机器默认最优。`cpu-profile-perf` 保留默认 DWARF 变体，当前机器子线程展开不足，因此没有用它制作自然热点结论。各 preset 使用独立缓存，不混用 Tracy 开关对象。

## 一次 perf 自然采集

```bash
python3 scripts/run_cpu_profile.py perf \
  --executable "$HOME/.cache/roam-profiling/build/cpu-profile-fp/tests/parallel_roam_transactional_lod_probe" \
  --snapshot benchmark-output/cpu-refinement/gmp-03/run-01/capture/test129-a-b4096-14.json \
  --mode trajectory-c-timing --replays 1 --callgraph fp --perf perf \
  --output benchmark-output/profiling/manual/test129-perf
```

探针在同一帧原计时边界前后与 perf enable/disable 握手；报告按 CLOCK_MONOTONIC 再过滤边缘样本。初始化、JSON、诊断在外。`--replays` 1～32 只为短任务采样量补充，每次重建同一种子再跑原八轮，不追加收敛帧替代真实负载。本轮根据初采 152/162 样本仅补采一次为 17/16，不能为得到更喜欢的热点反复重跑。

查看 `manifest.json`、`artifacts/report.md`、`summary.json`、`perf-report.txt`、`perf-script.txt`；原始 `perf.data`、记录时程序和构建身份也在 artifacts。先检查 complete、窗口数、有效样本、未知/缺栈和丢失。样本不足 2000 会标记不足，不是函数占比等于零；少于 30 的自身样本只能看作弱线索。

`self` 是机器函数自身采样，`inclusive` 含同线程调用链，父子不能求和。异步工作不在父线程调用栈里。主表不展开内联，特定热点可用官方 annotate/addr2line 深入；保留原二进制和采样时源码，不能对新二进制解释旧地址。

```bash
perf buildid-cache --add benchmark-output/profiling/manual/test129-perf/artifacts/recorded-executable
perf report --stdio --children \
  -i benchmark-output/profiling/manual/test129-perf/artifacts/perf.data
# 将函数名替换成 report 中的完整符号；不使用全局禁内联构建
perf annotate --stdio --symbol '完整函数符号' \
  -i benchmark-output/profiling/manual/test129-perf/artifacts/perf.data
```

## 一次 Tracy 自然采集

```bash
python3 scripts/run_cpu_profile.py tracy \
  --executable "$HOME/.cache/roam-profiling/build/cpu-profile-tracy/tests/parallel_roam_transactional_lod_probe" \
  --snapshot benchmark-output/cpu-refinement/gmp-03/run-01/capture/test129-a-b4096-14.json \
  --mode trajectory-c-timing --replays 1 \
  --capture "$HOME/.cache/roam-profiling/tracy-linux/tracy-capture" \
  --csvexport "$HOME/.cache/roam-profiling/tracy-linux/tracy-csvexport" \
  --output benchmark-output/profiling/manual/test129-tracy
```

将 snapshot 名换为 `peking547-a-b20000-14.json` 即为另一冻结场景。B 用 `trajectory-b-timing`，A 用 `trajectory-a-timing`；本轮完整函数时序主要验证 C4。A 的动态参考入口未专门标记，完整 frame 和内部函数仍可见，不以缺少 `gtp.update` 区间表示 A 没有工作。

工具在本机启动 capture、等待连接、运行原探针并排空后导出。连接最长等待 15 秒；中途断连/重连、目标失败、超时或缺尾标会标记不完整。仅该次捕获进程设置 TRACY_NO_EXIT，普通未连接运行不会无限等 capture。

`artifacts/capture.tracy` 可用同版查看器离线打开；程序化表为 `zones.csv` / `zones-self.csv` 与 `summary.json`。`profile.frame` 限定业务帧；`gtp.dispatch` 的文本是阶段名，`gtp.task` 的文本为阶段/chunk。`pool.task` 显示实际执行线程，线程数量按真正执行区间去重。导出 ID 不直接当 OS TID。

```bash
"$HOME/.cache/roam-profiling/tracy-linux/tracy-csvexport" -u \
  benchmark-output/profiling/manual/test129-tracy/artifacts/capture.tracy
```

不要给 Text 加换行或逗号；当前官方展开 CSV 不为所有动态文本转义。项目任务采用单行标签，避免 Text 与数值 Value 合成换行。区间时长是墙钟，包含抢占和等待；self 只扣已标记同线程子区间。没有系统调度事件时，不能把空白都称为算法依赖或锁开销。

## Classic / DOD 家族

同一 runner 将 executable 换为 `parallel_roam_transactional_lod_family_probe`，mode 改为 `classic` 或 `dod`，重放必须为 1。例如：

```bash
python3 scripts/run_cpu_profile.py tracy \
  --executable "$HOME/.cache/roam-profiling/build/cpu-profile-tracy/tests/parallel_roam_transactional_lod_family_probe" \
  --snapshot benchmark-output/cpu-refinement/gmp-03/run-01/capture/test129-a-b4096-14.json \
  --mode dod --replays 1 \
  --capture "$HOME/.cache/roam-profiling/tracy-linux/tracy-capture" \
  --csvexport "$HOME/.cache/roam-profiling/tracy-linux/tracy-csvexport" \
  --output benchmark-output/profiling/manual/dod-tracy
```

保留十四帧引导，ROI 只包围随后八个 BuildRenderData；返回帧独立质量评价在 ROI 外。家族输出直接位于 `artifacts/program`，GTP 重放输出位于 `artifacts/program/replay-0`。家族默认自动线程，不把它与 GTP C4 当成同策略配对。若另建 FP 家族目标，可以用同一 perf 后端；本轮没有为了短家族帧扩采矩阵。

## 关闭、输入与故障

- 普通 Release 将 `PARALLEL_ROAM_ENABLE_TRACY`、`PARALLEL_ROAM_PROFILE_SYMBOLS`、`PARALLEL_ROAM_PROFILE_FRAME_POINTERS` 设为 OFF；沿用原探针三个位置参数，不传 `--profile`。普通生产目标无需链接 ProfileSession。
- Tracy 构建不传 `--profile` 且不启动 capture，就是“启用未连接”成本；它仍有客户端后台线程，不是零成本模式。
- 输入不只有 snapshot：同目录的 `*-source.json`、冻结场景 CSV、相机 CSV，以及家族的原高度资产都不可缺少。它们来自此前冻结实验，不由 profiler 伪造。runner 今后将实际依赖及身份归档到 artifacts/inputs；早期数据的补归档明确标注时间，不冒称采集时证据。
- 默认最多 180 秒，原始单文件 512MiB，排空额外最多 10 秒。失败保留日志和不完整 manifest；禁止覆盖旧目录。工具只终止自己启动的进程组。
- 当前 perf 的 DWARF 子线程失败与 `/mnt/d` 活跃采集写入失败均保留限制；先用已验证 FP + 原生缓存，不能仅见命令退出零就判采样完整。
- 快验只跑受影响输入和必要故障检查。正式速度使用无采集构建；不要把工具连接、握手、导出、JSON 或离线质量评价混进算法时间，再以“移除工具”声称算法提速。
