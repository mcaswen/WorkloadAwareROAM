# CPU-CBT 关闭与实现归档小规划

> 类型：小规划；整理已关闭实验，不新增算法或公共接口
> 日期：2026-09-12
> 授权：用户要求将 CPU CBT 实现放入过时区域，随后明确先单独提交归档
> 状态：归档、文档同步、最小回归及性能核查完成；按用户要求单独提交
> 源码起点：`a3738d6`，实施前工作区干净

## 1. 目标与范围

将 CPU-CBT-01/02 从活动源码、测试和构建选项中移出，保留原始实现、来源、运行证据与未决问题。新的 CPU ROAM 研究定位及主线入口调整另行提交，不以本次归档授权新的 ROAM 优化或正式实验。

已阅读开发规范、规划规范、审查规范、CPU-CBT 大规划和两个小规划、代码事实、阶段审查与性能分析，并核对头文件、实现依赖、调用者、构建入口和 GATE-03 既有归档方式。

## 2. 文件归属与依赖决定

复用 `obsolete/` 的历史源码覆盖层方式，不新增可构建的归档模块。当前 CPU CBT 仅由两个实验驱动调用；生产 Classic/DOD、公共地形、线程池和渲染器没有对它的反向依赖。

| 动作 / 文件 | 职责与理由 |
| --- | --- |
| Move：`src/algorithms/cpu_cbt/` 的八个文件 | 原样移至 `obsolete/cpu_cbt/src/algorithms/cpu_cbt/`，保留状态、轮次编排、模板提交和网格实现 |
| Move：`tests/CpuCbtTests.cpp`、`CpuCbtProbe.cpp`、`CpuCbtExecutionSupport.h` | 原样移至 `obsolete/cpu_cbt/tests/`，保留夹具、实验入口及测试专属线程适配 |
| Create：归档中的 `cmake/ProjectOptions.cmake`、`tests/CMakeLists.txt` | 保存修改前完整构建配置；仅供历史检出恢复，不被当前 CMake 包含 |
| Modify：活动 `cmake/ProjectOptions.cmake`、`tests/CMakeLists.txt` | 删除原型选项和专属目标块；其他目标与源文件清单不变 |
| Create：`obsolete/cpu_cbt/README.md`、`manifest.json` | 记录关闭决定、源提交、逐文件散列、参考依赖及恢复边界 |
| Update：CPU-CBT 原规划、事实和审查 | 增加关闭状态，事实文档的源码链接指向归档；历史结果保留原位置 |
| Create：`docs/reviews/cpu_cbt/cpu_cbt_archive_review.md` | 归档完整性、构建排除、最小回归及性能证据核查 |

不移动或修改 `third_party/RoamTesting`，不复制其整个仓库。不移动共享线程池、GATE-01/02 或 Classic/DOD 代码。不修改归档 C++ 的注释和算法内容。原始数据继续留在已忽略的 `benchmark-output/cpu-cbt/`，不丢弃已有失败或不利结果。

CPU-CBT-02 的局部性能问题随实验关闭而停止投入，保留未修复事实；这不等于性能修复完成，也不影响 ROAM 主线的独立评价。

## 3. 实施与最小验证

1. 修改前构建现有 `parallel_roam_runtime_performance_probe`，保存程序、配置和输入身份；用 `test129-a-b4096 / serial-incremental / diagnostics-off` 运行一个预热进程和五个计量进程，冻结中位数/P95 的工程门槛。
2. 生成清单，核验全部原始路径位于本工作区；原样迁移专属文件，保存两个构建文件快照，再移除活动目标块与选项。
3. 完成归档入口与 CPU-CBT 历史关闭说明。重新配置一个既有 OpenGL 构建及原 CPU-CBT 无窗口构建，确认不再注册原型目标和 `CpuCbt` 测试。旧构建目录可能保留历史二进制；它们不代表活动构建入口。
4. 构建性能探针及 `dod_pass_policy` 对应程序，只运行该策略 CTest。归档逐文件核验 SHA-256 和 UTF-8/LF 规范化散列；检查活动引用、文档链接和 `git diff --check`。移出源码会改变注释覆盖的统计分母，因此另运行一次现有覆盖检查，不修改历史注释。
5. 使用修改前保存程序 A 与修改后程序 B，在同一环境预热一次，按 AB/BA 交替完成五对独立进程。复用既有 CSV 读取、语义核验和百分位统计；报告完整构建/CPU 更新的中位数、P95、配对差及重复间范围。诊断开关固定为 `EnablePassEvidence=false`、`EnableTopologyValidation=false`、`EnableTopologyPairEvidence=false`；源码、输入与计时边界不变。

性能产物保存到 `benchmark-output/cpu-cbt/archive-20260912/`；一次性采集脚本随产物保存，不新增项目级实验框架。遵循开发规范第 7.3 节：门槛为 `max(0.05 ms, 修改前统计值的 5%, 修改前五值极差)`，只有越线才允许一次同范围复测，不追逐微秒差异。

本次风险是迁移遗漏和构建残留，不是算法行为变化。复用既有 CPU-CBT 正确性与自然轨迹证据，不重跑已关闭原型、完整 P5、质量实验、全量 CTest 或双后端性能矩阵。

## 4. 实现结果

2026-09-12 按上述范围完成：11 个专属源码/测试文件原样迁移，两个构建文件保存历史快照；13 份文件的字节与规范化散列均一致。活动原型开关、库、程序与 CTest 注册移除，第三方参考保持原提交与干净状态。

CPU-CBT 规划、代码事实和审查已同步关闭处置；局部性能问题以未修复状态随原型停止投入。原 P5 门槛和结果保留，不属于本次提交的修改范围。

OpenGL 及原无窗口构建重新配置后均无 CPU CBT 目标或测试；只构建探针和 DOD 策略程序、运行一个 `dod_pass_policy` CTest。非 CPU-CBT 生产源码和探针二进制前后相同；五对独立进程的完整构建与 CPU 更新中位数/P95 均未触发工程恶化门槛，未追加复测。

详细身份、命令、结果和结论见[归档审查](../../reviews/cpu_cbt/cpu_cbt_archive_review.md)，原始证据在 `benchmark-output/cpu-cbt/archive-20260912/`。用户随后要求先单独提交 CPU-CBT 归档；新研究定义页、README、总计划及其他主线文档调整留在工作区，未纳入本次提交。未开展后续研究实验或 CPU-CBT 修复。
