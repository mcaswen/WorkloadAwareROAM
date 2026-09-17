# QPC-06G 坐标与参考高度分离小规划

## 1. 问题与目标

06F中`Cover`仍占Sierra已解析采样inclusive 27.70%。`ExactCoordinate`经`ExactReference`计算双线性高度，虽然边判断只读坐标。`CoordinateBounds`有同样职责混合。本阶段只删除坐标查询强制触发的高度求值，保持坐标、区间端点、覆盖顺序、精确谓词和质量结果完全一致。

## 2. 文件、数据与边界

- Extend `TransactionalQualitySampleEvidence.cpp`：在参考尚未存在时，只构造原公式的两个坐标；参考已存在时复用其坐标。所有权仍为单样本单域，没有全局缓存。
- Reuse `TransactionalQualityEvaluation.h`的`Coordinate`：输出域仍使用相同世界坐标表达式，不改变float曲面。
- Extend `TransactionalPointwiseQualityTests.cpp`：用未改的`Reference<T>`作独立表达式oracle，覆盖先坐标后参考与反向调用、共享边、边界和两域；检查坐标查询不增加高度参考计数。
- Reuse 原账本`BoundsReferences/ExactReferences`及诊断/计时runner；必要编排脚本放`scripts/`，不进入生产。

不改变矩阵、采样、误差式、回退、Fit或线程。无需新增生产文件或通用证据框架。

## 3. 推导与复杂度

坐标函数对第三分量无依赖。将`Coordinate(Reference<T>)`改成`Coordinate({u,v,0})`，其中u/v仍按原T域除法得到，则核心域和公开域逐运算相同。参考高度在真正需要时仍用原`Reference<T>`，未近似计算。区间域不能代数重排；有理域不能先用double除法再转有理数。

设坐标请求数c、其后实际参考高度请求数h≤c，旧路径做c次高度构造，新路径只做h次；后续参考可能重算两个坐标，额外常数也必须计费。最坏仍是原样本×面访问界，不宣称复杂度阶数下降。推导及反例写入研究报告并更新总索引。

## 4. 实施与验收

先冻结06F程序及同条件当轮基线；局部实现后构建逐点、面证据、高度拒绝测试和原生程序。两个源高自然轨迹各一次、旧Fit默认路径代表一次，比较96帧逻辑和公开网格哈希。Sierra补一次工作诊断，核对参考构造下降而样本/接受人口保持。性能按共同队列协议；无收益或出现持续退化则撤回实现，不追加数值核。

交付`qpc_06g_coordinate_evidence_results.md`（含推导/成本）、局部事实与审查、精简数据；结果和大规划同步后单独提交。

## 5. 实施结果

已完成。三项相关专项、原生构建和三条96机会输出对照一致。Sierra/Canyon完整移动110.643→106.031、67.743→62.744ms；精确参考构造减少83.57%，样本/覆盖/接受人口保持。源码、工作计数和有限收益因果见[报告](../../research/cpu_refinement/qpc_06g_coordinate_evidence_results.md)。旧Fit代表没有工程回退，保存所有原始结果，不追加微差复测。无需新增生产文件；保留单样本生命周期，进入06H。
