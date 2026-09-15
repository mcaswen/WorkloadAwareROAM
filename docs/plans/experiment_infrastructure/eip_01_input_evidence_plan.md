# EIP-01 输入与证据契约小规划

> 2026-09-15；Minor Plan；上层：[实验基础设施大规划](experiment_infrastructure_major_plan.md)
> 用户已确认大规划并授权按阶段自主闭环；沿用当前任务逐阶段提交授权，完成后提交再继续

## 目标与边界

建立两项历史地形的来源目录、版本化实验配置与运行记录，使后续新增资产与回放有明确输入契约。本阶段不改变算法/相机/渲染运行行为，不下载新地形。
现有应用仅能选择两个固定高度图；本阶段提供独立加载与校验能力，应用选择在EIP-02接入。

## 文件与职责

- Create：`assets/experiments/terrain_catalog.json`、`licenses/legacy_assets.md`，记录真实尺寸、文件SHA、规范uint16样本SHA、尺度和来源缺项
- Create：`configs/experiments/schema/`及两历史case，限定版本、ID、算法/高度策略、预算、线程、采样和路径
- Create：`scripts/experiment_infrastructure/catalog.py`，SHA/尺寸/位深校验、目录引用解析，不运行算法；`asset_preview.py`只制作资产信息页
- Create：`ExperimentCase.h/.cpp`，读取Python解析后的窄JSON配置并验证，依赖现有Boost.PropertyTree；`ExperimentRecords.h/.cpp`保存运行/帧证据，不负责统计
- Create：`cmake/ExperimentInfrastructure.cmake`，显式开关默认关闭；测试/应用只在开启时链接，旧构建不增加Boost要求
- Create：`tests/ExperimentInfrastructureTests.cpp`，检查非法配置、实际HeightMap样本与重复运行目录；不链接完整算法
- Modify：根及tests CMake、`.gitignore`，接入目标并忽略原始结果
- 文档：本小规划、事实、审查及结果；不向现有大文件加入离线处理

C++读取resolved case；Python先校验清单版本/资源/哈希，再生成resolved case。样本哈希统一U16 little-endian row-major；C++输出相同编码用于交叉核对，避免把图片文件SHA当样本身份。外部case路径限定项目/产物根，所有数值要求有限且在明确区间。

## 实施与验证顺序

1. 保存旧两资产直接解码/哈希的单进程短基线；不使用旧正式实验5+30
2. 实现目录、schema、Python校验与预览，实际查看两资产信息图
3. 实现C++边界与独立测试：旧文件实际加载、样本字节一致、非法预算/未知枚举/损坏引用/重复运行目录拒绝
4. 对相同资产复测原直接加载路径，新目录校验费用单列；报告版本/环境、绝对差与范围
5. 回填大规划、事实和架构审查后进入EIP-02

预期采集少于2分钟。只跑新增边界测试及Python输入测试，不运行拓扑算法、全CTest或两后端矩阵。未触碰运行热路径，性能证据以同源输入读取及新增离线处理费用为限。

## 验收与状态

来源不明的历史资产明确标记不可据此证明公开分发许可。文件名的513不能覆盖Peking实际547²。
Agent须实际打开预览；用户视觉字段保持待审，不代签。性能采用开发规范7.3，若有明确回归先定位，不追逐几微秒。

## 实现情况

已完成输入/schema、两项历史资产目录、Python解析与预览、C++边界与独占记录器、定向交叉验证及忽略规则。
证据：`benchmark-output/experiment-infrastructure/eip-01/`；[事实](../../codebase/experiment_infrastructure/eip_01_input_evidence_facts.md)、[审查](../../reviews/experiment_infrastructure/eip_01_input_evidence_review.md)。
两资产实际U16样本一致；已打开查看最终信息图。仅开发快验，无算法/帧时间结论。用户视觉待审。
新增依赖安装到Ubuntu用户cache，不改变系统Python；命令使用`PYTHONPATH=/home/mcaswen/.cache/roam-experiments/packages:./scripts`。
