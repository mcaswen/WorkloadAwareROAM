# GATE-03 过时实现归档

用户决定关闭本轮 GATE-03，并将实现移入过时文件夹；2026-09-12 完成归档整理。本目录保存关闭前的源码，**不加入当前生产或测试构建，不继续维护实验接口**。

关闭原因是当前保守模型只发现有限独立工作，继续投入的价值不足以支持扩大本轮实验。17 个变体完整、1 个资源截断、2 个未运行；质量尚未配对评价。该决定不表示一般排序松弛或约束批量细分已被否证。

## 保存范围

目录内按项目原相对路径保存六个文件，SHA-256 及完整基准提交见 [manifest.json](manifest.json)。`sha256` 记录关闭时原始字节；`normalizedSha256` 对文本统一 UTF-8/LF 后计算，供 Git 在 Windows 检出时核验，避免自动换行转换制造内容差异。

| 文件 | 保存理由 |
| --- | --- |
| `src/algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.h/.cpp` | 本阶段独有的快照计划、单线程反事实执行、逻辑投影和有限排列审计 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp` | 保存指定根步进及共用控制预览所在的完整历史文件 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h` | 保存上述历史实验接口及所依赖的值类型 |
| `tests/OrderingGateTests.cpp` | 保存本阶段解析夹具、`--relax-natural` 驱动和记录输出 |
| `tests/CMakeLists.txt` | 保存当时的实验编译清单，供独立历史检出重建 |

其中四个共享文件在活动目录中已恢复到 `a0f3551`，只有本目录保存其 GATE-03 版本。GATE-01 操作观察/严格步进、GATE-02 双线性主参考和已完成校准能力仍保留在活动目录。

## 历史证据与恢复

- [阶段规划及关闭处置](../../../docs/plans/ordering_relaxation/gate_03_snapshot_relaxation_plan.md#8-用户决定关闭与实现归档)
- [历史代码事实](../../../docs/codebase/ordering_relaxation/snapshot_relaxation.md)
- [阶段审查与关闭核查](../../../docs/reviews/ordering_relaxation/gate_03_snapshot_relaxation_review.md)
- 原始运行证据仍位于 `benchmark-output/ordering-relaxation/gate-03-20260911/`；运行输出受 Git 忽略，不能假定其他检出含有这些文件。

需要重建历史实现时，在独立的 `manifest.json.baseCommit` 检出中，按清单把六份文件覆盖到其原相对路径，再使用原 `relwithdebinfo-fetch` 构建配置。这里保存的是历史源码覆盖层，并非可单独构建的模块；不要把此处的 CMake 文件加入当前工程。

筛查使用 `screen-1/` 冻结版本，本归档保存随后修正资源终止记录和注释后的交付源码，两者区别已记录在历史事实和审查中。恢复源码不意味着旧实验已重新运行。
