# 实验基础设施使用手册

本入口负责资产、冻结路线、有限回放、真实画面、独立质量、分析与报告。它不改变算法默认高度策略，也不自动运行正式论文矩阵。

## 1. 已有内容与构建

- 12个地形：2历史、6确定性生成、4 USGS DEM；见[地形catalog](../../../assets/experiments/terrain_catalog.json)
- 6个材质：legacy、中性、方向UV、岩石、土壤、草岩；见[材质catalog](../../../assets/experiments/material_catalog.json)
- 65条冻结路线，5种新模板和5个历史导出；见[相机catalog](../../../configs/experiments/cameras/catalog.json)
- [case目录](../../../configs/experiments/cases)：预算、深度、阈值、线程、高度策略、路线、材质均显式保存

CMake需开启PARALLEL_ROAM_ENABLE_EXPERIMENT_INFRASTRUCTURE；新算法还需PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME及现有Boost头文件。该实验开关默认OFF，旧应用默认配置不自动增加实验模块。

Windows使用项目原生MSVC/CMake环境，分别构建OpenGL和D3D12目录，不能在同一cache中切换后端后混用程序。已有transactional-platform与transactional-platform-d3d12 preset可作为配置基础，额外加上述实验开关并指定独立-B。Linux只构建CPU回放/质量probe，不提供实际平台截图。普通实验程序不要启用perf/Tracy。

本次已构建可执行文件：
- Windows GL：build/relwithdebinfo-fetch/bin/ParallelROAM.exe
- Windows DX：build/relwithdebinfo-d3d12-fetch/bin/ParallelROAM.exe
- Linux CPU：/home/mcaswen/workload-roam-nmp01p-build/tests/parallel_roam_experiment_cpu
- Linux质量：同tests下parallel_roam_transactional_platform_quality_probe

以下命令在Ubuntu WSL、仓库根目录执行。Python依赖版本见[requirements](../../../scripts/experiment_infrastructure/requirements.txt)。可在独立venv安装，不修改系统Python。本机已有target安装可直接使用：

~~~bash
cd /mnt/d/CPP-Projects/WorkloadAwareROAM
export PYTHONPATH=/home/mcaswen/.cache/roam-experiments/packages:$PWD/scripts
python3 scripts/run_experiment.py --help
~~~

原生窗口由已有runner隐藏启动，图像仍来自真实图形后端；截图只包含渲染区域，不包含桌面/GUI。Windows调度与Linux CPU运行分别记录，不能混作同一总体。

## 2. 资产和简单材质

运行库已经带规范化U16输入，不必为每次实验联网。生成器用数学函数与固定种子；真实DEM使用服务端投影、F32冻结导出和端点/轴映射，再量化为U16。运行参考是这份U16及双线性插值，不是原始点云，也不是TerrainMeshBuilder固定对角线曲面。

~~~bash
python3 scripts/run_experiment.py catalog --output benchmark-output/experiment-infrastructure/my-asset-review
python3 scripts/run_experiment.py resolve --case configs/experiments/cases/canyon-dod-return96.json --output benchmark-output/experiment-infrastructure/my-case.json
~~~

重新生成/下载属于资产准备，按需调用experiment_infrastructure.terrain_generation、dem_import、material_assets；不要为了重跑实验重写catalog或下载缓存。已有文件内容不同会报错，不应删除错误记录再冒充同一版本。新来源/新裁剪应使用新ID和哈希，补来源/许可/量化/NoData记录并实际查看。

自然输入dem-sierra/dem-canyon/dem-hills/dem-lowland预选于新算法运行前。低地DEM含真实服务镶嵌/水体特征，保留此局限。历史test129/Peking的再分发许可未确认；不要根据仓库存在就宣称可公开分发。Poly Haven只使用CC0原始材质文件，网页图片许可不混同。

GUI“实验资产与路线”面板可换地形和材质：换地形会Reset，换材质不会。中性材质用于几何检查，UV检查用于方向/平铺，地表材质用于展示，不能只用纹理遮盖质量差异。

## 3. 相机录制与冻结

GUI记录当前姿态、删除末点、指定停留、导出配方与冻结CSV。载入后播放/暂停/单步；“重放起点”明确Reset，“自由飞行”退出回放。路线返回与停留不会Reset。视口不符会报错，不能用不同投影冒充同一相机。

新路线使用C++ CameraRecipe生成实际float矩阵；Python不重算投影。历史路线通过C++ legacy导出保留旧公式。单路线入口示例（原生程序接收Windows路径）：

~~~bash
build/relwithdebinfo-fetch/bin/ParallelROAM.exe --experiment-camera build dem-canyon 'D:/CPP-Projects/WorkloadAwareROAM/configs/experiments/cameras/recipes/reveal-return.json' 'D:/CPP-Projects/WorkloadAwareROAM/benchmark-output/experiment-infrastructure/my-camera.csv'
~~~

新文件审查通过后，记录terrain样本SHA、recipe、CSV SHA并加入相机catalog。已有65条冻结路线无需重导出。名义秒仅控制展示，算法始终按更新机会推进；播放器流畅不代表达到该FPS。

## 4. 独立运行三种模式

每次使用全新输出目录，失败/超时也保留。例子使用同一个case分别运行，不能在运行间修改源码、程序或输入再称同executionId：

~~~bash
python3 scripts/run_experiment.py run --case configs/experiments/cases/canyon-transactional-return96.json --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe --mode timing --output benchmark-output/experiment-infrastructure/demo-timing
python3 scripts/run_experiment.py run --case configs/experiments/cases/canyon-transactional-return96.json --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe --mode visual --output benchmark-output/experiment-infrastructure/demo-visual
python3 scripts/run_experiment.py run --case configs/experiments/cases/canyon-transactional-return96.json --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe --mode quality --output benchmark-output/experiment-infrastructure/demo-quality
~~~

D3D12使用对应程序并加--backend d3d12。CPU用Linux目标、加--cpu；CPU没有上传/Present，不能制造这些数值。运行manifest保存源码ZIP/哈希、commit与dirty、程序SHA、实际输入副本、构建cache、OS/CPU、模式和原始产物。

timing不截图、不做全Q独立评价；visual包含GPU回读等待，不用作正式正常时间；quality只导出实际CPU网格，求值另运行。逐帧全mesh校验/hash在正常计时外，可能影响下一帧cache，各模式同样记录此边界。输出frames.csv保留冷启动、预热、停留、零事务和未归属成本。

每运行180秒/8GiB，正常计时顺序执行；构建、出图、质量求值、profiler不可同时竞争CPU。模式核对使用runner.compare_modes，检查相同executionId及全部帧实际mesh和公开工作计数；这不是完整逻辑决策trace证明。

## 5. 独立质量与函数证据

~~~bash
python3 scripts/run_experiment.py quality --run benchmark-output/experiment-infrastructure/demo-quality --probe /home/mcaswen/workload-roam-nmp01p-build/tests/parallel_roam_transactional_platform_quality_probe --frames 2 48 80 95 --output benchmark-output/experiment-infrastructure/demo-evaluation
~~~

只评价已导出的帧，缺失记not-exported。质量k=0采样、完整errors.f64、稀疏位置CSV与全Q最大见证分开。Emax是sampled maximum；RMS为可见参数域样本等权，不是屏幕面积加权。Dmax调用quality.pointwise_pair(candidateDir, referenceDir, outputJson)，必须同reference/视图/采样域；它是逐点误差差值最大，不是两个Emax之差。

perf/Tracy复用FPR工具。使用独立带符号的Linux CPU程序和profile子命令，不拿profile时间作正常速度：

~~~bash
python3 scripts/run_experiment.py profile perf --case configs/experiments/cases/peking-transactional-return24.json --executable /home/mcaswen/.cache/roam-profiling/build/cpu-profile-fp/tests/parallel_roam_experiment_cpu --perf perf --output benchmark-output/experiment-infrastructure/demo-perf
~~~

Tracy改为profile tracy，并显式--capture与--csvexport，版本0.14.1。旧环境若需特定perf路径照原FPR配置传入。看ROI有效性/缺栈/样本数后再解释占比。self与inclusive不可叠加；采样数不是调用次数；不同程序/捕获时钟不拼接。

## 6. 同一analysis生成全部图表与报告

~~~bash
python3 scripts/run_experiment.py analyze --runs benchmark-output/experiment-infrastructure/demo-timing benchmark-output/experiment-infrastructure/demo-visual benchmark-output/experiment-infrastructure/demo-quality --quality benchmark-output/experiment-infrastructure/demo-evaluation --output benchmark-output/experiment-infrastructure/demo-analysis
python3 scripts/run_experiment.py report --analysis benchmark-output/experiment-infrastructure/demo-analysis/analysis.json --output benchmark-output/experiment-infrastructure/demo-report
~~~

注意analyze的--quality接受包含quality-index.json的目录，--pairs接Dmax JSON。--history接显式schema列表，旧TPI/SVE/FPR不隐式猜列或并入新总体。

报告包含HTML/Markdown、PNG/SVG/PDF、FigureSpec、真实帧播放器、固定裁剪、误差热图、全函数表及原始链接。图表从analysis取数；每图记录analysis SHA和显示口径。图/视频是独立产物，不能在编辑图片后仍沿用原图SHA。HTML可离线打开，播放器不需要服务器；图片已复制，raw/source链接仍依赖保留相对证据目录。

研究图不要删慢样本、不补零、不把缺失说成无代价；同任务同结果的1→8可称有限并行对照，Classic/DOD/Transactional跨算法只有成本和质量设计点。独立进程才是统计单位，本次单进程开发验收不生成正式CI。

## 7. 正式矩阵只生成清单

~~~bash
python3 scripts/run_experiment.py suite --case configs/experiments/cases/canyon-transactional-return96.json --budgets 20000 50000 100000 200000 --workers 1 4 8 --algorithms dod transactional --prefixes fixed64 scaled --output benchmark-output/experiment-infrastructure/my-suite
~~~

这会生成48个case和suite.json，不执行。DOD不适用heightPolicy，生成器写schema要求的fit占位；不会把它解释成DOD高度优化。生成顺序不是正式随机/交错执行次序；正式重复数、次序、质量/预算利用率门槛应另冻结。

## 8. 视觉与验收记录

依次看：资产方向和比例→中性/UV/展示材质→真实相机序列→返回前后固定裁剪→独立误差→阶段/函数/时序图。热图灰格表示没有导出观察点，色标是已观察误差，不是连续上界。出现真实几何坏区保留，不靠改色标或删镜头通过。

四种结果独立：程序化检查、Agent实际视觉、用户视觉、算法质量；浏览器不可用另列，不能以播放器逻辑单测代签页面交互。当前浏览器工具无可用surface，实际PNG与原生后端捕获已检查，真实浏览器交互仍待恢复。

原始数据只放明确忽略的benchmark-output/experiment-infrastructure；小资产、清单、脚本与文档保留供审查。不要自动commit/push。结果索引见[验收报告](acceptance_results.md)。

## 9. FER-01 正式重复与报告实例

[FER-01完整报告](../../research/experiment_infrastructure/fer_01_results.md)展示在上述基础设施上执行一次有限正式实验的方法，协议在 `configs/experiments/formal/fer_01/protocol.json`。`formal_study.py`按预冻结次序运行20配置×3独立进程，正常结束后才采视觉与离线质量；`formal_report.py`从同一EIP analysis归约进程重复并生成19组图和数表，不继承开发验收的单进程判词。

已有原始根 `benchmark-output/experiment-infrastructure/fer-01` 应保留。真正重跑使用新的实验根并重新冻结身份，不能删除本轮证据。报告中的CSV、PNG和精简汇总已随阶段保存于 `docs/research/experiment_infrastructure`；完整HTML、SVG/PDF、逐帧网格及误差数组留在原始根。当前实例没有新增perf/Tracy采样，不能把历史函数占比混入本轮平台时间。

配置可选 `flipRecovery` 默认关闭，仅允许Transactional搭配immutable；开启时进入任务身份，并由回放适配传给已有公共开关。独立 `flip-recovery.csv` 保存触发、尝试、认证、冲突和执行数量，不改变原60列 `frames.csv`，也不把净零翻边并入预算交换分母。
