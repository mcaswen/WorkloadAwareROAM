# CPU-CBT 过时实现归档

2026-09-12，用户决定将本轮 CPU CBT 实现移入过时区域，研究重点回到 ROAM 的多阶段执行组织、CPU 并行优化与性能边界。本目录保存 `a3738d6` 的原型源码和构建快照，**不加入当前应用、库或测试构建**。

## 关闭依据与结论边界

CPU-CBT-01/02 已完成持续串行更新、局部模板并行及三档对照。两个自然输入的逐轮结果一致，并观测到真实多线程模板填写；但新串行填写仅占完整更新累计成本约 0.066% / 0.209%，所测完整更新没有明确加速证据。该结果仅约束当前存储、编排和并行边界，不否定一般 CPU 批量更新，也不否定 Classic/DOD ROAM 的并行优化价值。

test129 新串行细分提交的局部性能问题**未修复，随原型归档停止投入**；保留原分析和数据，不追认为性能验收通过。归档也不表示已经开展与 ROAM 的独立几何质量比较。

## 保存范围

按原相对路径保留 13 个文件，详见 [manifest.json](manifest.json)：

- `src/algorithms/cpu_cbt/`：状态、更新、模板填写和网格的八个 `.h/.cpp` 文件，原样移动。
- `tests/CpuCbtTests.cpp`、`CpuCbtProbe.cpp`、`CpuCbtExecutionSupport.h`：解析夹具、自然轨迹驱动与线程适配，原样移动。
- `tests/CMakeLists.txt`、`cmake/ProjectOptions.cmake`：关闭前完整构建配置的副本，仅供历史恢复。

清单中的 `sha256` 对原始字节计算，`normalizedSha256` 对 UTF-8 文本统一 LF 后计算，避免不同检出的换行转换影响核验。算法、夹具和注释没有随迁移改写。

## 历史证据与恢复

- [大规划](../../docs/plans/cpu_cbt/cpu_cbt_minimal_implementation_plan.md)、[第一阶段](../../docs/plans/cpu_cbt/cpu_cbt_01_serial_update_plan.md)、[第二阶段](../../docs/plans/cpu_cbt/cpu_cbt_02_parallel_bisect_plan.md)。
- [历史代码事实](../../docs/codebase/cpu_cbt/cpu_cbt_update.md)、[第二阶段审查](../../docs/reviews/cpu_cbt/cpu_cbt_02_parallel_bisect_review.md)、[局部性能分析](../../docs/reviews/cpu_cbt/cpu_cbt_02_performance_analysis.md)。
- [归档小规划](../../docs/plans/cpu_cbt/cpu_cbt_archive_plan.md)、[归档审查](../../docs/reviews/cpu_cbt/cpu_cbt_archive_review.md)。
- 原始产物位于 `benchmark-output/cpu-cbt/cpu-cbt-01/` 和 `cpu-cbt-02/`，仍由根 `.gitignore` 忽略；其他检出未必含有这些本地文件。

如需重建，在独立的 `manifest.json.sourceCommit` 检出中恢复上述文件，按原阶段规划配置 `BUILD_APP=OFF`、`BUILD_TESTS=ON`、`PARALLEL_ROAM_BUILD_CPU_CBT_PROTOTYPE=ON`。本目录不是独立可构建项目，不要将这里的 CMake 文件直接包含进当前工程。

还需要原阶段的 GLM/STB，以及独立 `third_party/RoamTesting` 工作副本中实际参与编译的参考子集。参考提交及关闭时状态见清单，源码散列和来源说明见原阶段证据及参考仓库的 `THIRD_PARTY_NOTICES.md`。参考仓库没有随本次归档复制或修改，归档不改变其来源与发布条件。

旧 `build/cpu-cbt-01/02` 中可能仍有历史程序或生成文件；当前 CMake 已不再注册原型选项、目标与 `CpuCbt` 测试。恢复历史文件不表示重新运行过旧实验，也不自动授权重新开启该路线。
