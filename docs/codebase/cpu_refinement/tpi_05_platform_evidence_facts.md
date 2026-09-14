# TPI-05 平台持续回放事实

2026-09-15；依据[小规划](../../plans/cpu_refinement/tpi_05_platform_evidence_plan.md)，完整结果见[研究报告](../../research/cpu_refinement/tpi_platform_results_and_headroom.md)。

## 文件与依赖

- `benchmark/TransactionalPlatformReplay` 使用既有Window/GraphicsBackend/TerrainRenderer，按冻结轨迹一次机会一次更新。只显式诊断入口执行，不进入正常Application循环，不链接事务研究oracle。
- ApplicationCommandLine只新增入口分流，main转发原参数；实际有限参数和可用算法在Replay中校验。CMake两个full app都加入同一个Replay源。
- TerrainRenderer只新增只读当前CPU mesh借用，止于下一Build/Reset/销毁；调用方不修改、不缓存跨帧裸指针。未更改运行算法、上传、旧热路径或调度。
- `mesh_quality/PlatformMeshArtifact`是诊断文件协议：明确小端、版本、计数、13个float顶点字段、索引、矩阵、尺度、drawable及深度约定。没有按结构体padding序列化。读取限定最大槽和索引规模。
- 已有MeshQualityEvaluator扩展ZO、可选逐点误差和等样本权RMS；旧默认NO、原采样顺序/哈希不变。NaN保留异常，-1仅指参考不可见；无可见样本时RMS为空。评价器仍独立于生产核心。
- `tests/TransactionalPlatformQualityProbe`作为离线工具单独链接HeightMap/TerrainMeshBuilder/evaluator；规则网格只给采样域，raw双线性是reference。实际测量float xyz，不重采拟合顶点。
- Python runner负责命令、二进制/源/资产身份和超时/RSS，report仅核对身份、归约原始CSV及配对Dmax，无重测或挑帧逻辑，不引入新的数值依赖。

## 已验证结果

原生OpenGL两输入B1/C8/DOD8及Peking Classic各24帧；D3D12 Peking C8/DOD8各8帧。正常路径均退出0。test129/Peking分别94/181次预算交换，D3D12短轨迹22次；所有受检索引/有限坐标/既有绕序和预算合法。

2组B/C共48帧公开网格哈希及决策摘要一致；5组normal/export共120帧一致，25份artifact被离线读回后哈希一致。25次k0质量评价为sampled_only，异常计数均零；10组Dmax配对采样身份/可见域相同。不能用这些有限检查替代所有拓扑/所有输入证明。

MeshQualityEvaluator定向测试新增解析RMS、逐点误差、NO/ZO等价、缺失NaN、不可见-1；运行该测试及ApplicationCommandLine入口测试通过。两个原生full app定向构建通过；没有重跑全部CTest。源注释覆盖15.7%，renderer12.2%，DOD/Classic20.0%。

## 性能与风险归属

本阶段无核心热路径更改，旧平台性能回归证据复用TPI-04；本阶段提供同环境新/旧算法有限运行成本。B1→C8暖CPU约3.47×；test129 C8 11.945ms 对DOD .663ms，Peking C8 40.622ms 对DOD42.285ms。后者质量不同，不是等任务加速。

Peking/15 sampled Emax66.081px，返回/23仍31.126px；DOD对应.419/.442px。恢复窗口未达到原质量，禁止按性能接近宣称实用性通过。没有因此修改HeightGuard、拟合、前缀或输入。

原始有效采集run-02约27.89秒，离线评价约10.78秒；峰值观测RSS237.3MiB。OpenGL GPU query/wait原始占位零在报告里改为N/A，D3D12 GPU是延迟结果。帧间哈希检查影响缓存/重叠，故帧包络不称无探针交互FPS。

## 实施中修正与限制

run-01检查器误套Classic上向绕序，新核心使用参数域CCW、单独朝上法线；两后端不剔除背面。修正按算法既有绕序检查，未修改几何。启动器提前保留进程handle，修复进程结束后ExitCode缺失。原始失败留存，不合并为有效采集。

新算法接入不再依赖每帧Legacy目标，但仍依赖一次当前视图DOD冷种子。程序化回放不含GUI，不证明视觉popping可接受；无设备移除、无任意输入性能保证。更大200k点复用SVE历史边界，不新增平台压力矩阵。

## 可复算命令

所有命令在Ubuntu中执行；native应用经WSL interop运行。输出目录必须为空，下面路径是示意的新run目录，不覆盖已归档run-02。

```bash
python3 scripts/run_transactional_platform.py collect --output benchmark-output/cpu-refinement/tpi-05/new-run --exe build/relwithdebinfo-fetch/bin/ParallelROAM.exe --backend opengl
python3 scripts/run_transactional_platform.py collect --output benchmark-output/cpu-refinement/tpi-05/new-run --exe build/relwithdebinfo-d3d12-fetch/bin/ParallelROAM.exe --backend d3d12
python3 scripts/run_transactional_platform.py quality --output benchmark-output/cpu-refinement/tpi-05/new-run --exe /home/mcaswen/workload-roam-nmp01p-build/tests/parallel_roam_transactional_platform_quality_probe
python3 scripts/transactional_platform_report.py benchmark-output/cpu-refinement/tpi-05/new-run
```

交互应用用 `--algorithm transactional --transactional-workers 8 --transactional-prefix 160 --transactional-donors 160` 显式启用；构建预设transactional-platform/transactional-platform-d3d12默认不替换旧Classic/DOD。交互参数不等于上述冻结Peking场景，资产/预算/尺度需按相应实验设置。
