# CEI-01 统一观测、矩阵和配对小规划

日期：2026-09-16。关联[大规划](cbt_experiment_infrastructure_major_plan.md)。状态：实现、定向验证和审查完成。

## 目标与问题

让普通`run → analyze`直接消费CBT，同一suite可生成CPU与GPU适用配置。保留缺测，不增加GPU全量读回，不更改算法。报告显示留给CEI-02，本阶段先固定真实数据契约。

当前`analysis.summarize`直接执行`faces/budget`会在CBT timing失败；其CPU阶段和成本比逻辑也会把CBT空值误解释成零工作。CBI专用GPU去重函数应共享，不能在正式报告中重新实现一套。

当前`runner.compare_modes`要求CPU当前hash逐帧相同。GPU计时没有hash，不能将其和visual不等称算法错误，也不能把两个空hash相同称精确结果一致。

当前`suite`展开预算×线程×算法×前缀，CBT不适用这些CPU维度；`quality.pointwise_pair`却要求含预算的工作负载身份相同，阻止实际N不同的同域误差比较。

## 文件边界

| 文件 | 处理 | 职责 |
|---|---|---|
| `gpu_observations.py` | Create | 解析GPU资源/采样代，去重、归属机会和阶段覆盖，不读取网格 |
| `result_adapters.py` | Extend | 输入协议与故障/捕获校验，不做统计胜负判断 |
| `analysis.py` | Extend | 可空N、CPU预算/GPU容量、设备边界、每进程分组、质量有效性 |
| `runner.py` | Extend | 分CPU精确公开结果与GPU有限输入/机会证据的模式核对 |
| `quality.py` | Extend | 共享逐点配对原语，显式同域跨预算，保留旧严格默认 |
| `suite.py` | Extend | 只展开各算法适用维度，清理不适用策略字段 |
| `run_experiment.py` | Extend | CBT面积/容量及显式配对CLI，薄参数传递 |
| `cbt_report.py` | Extend | 委托共享GPU采样/逐点计算，保留历史报告入口 |
| 定向Python测试 | Extend/Create | CPU兼容、GPU缺测/错代、矩阵去冗余、配对拒绝 |

不修改C++/HLSL/公共渲染接口；不把CBT细节塞入CPU算法实现。所有状态是离线加载的运行记录，原始文件只读。

## GPU观测契约

GPU记录按`(resourceGeneration, topologyGeneration)`映射到真实更新机会。计算、绘制、分类各自使用其采样代，不能把最新回来的结果填入当前帧。

时间以有效非负有限数值为前提。相同采样代重复返回只统计一次；相同代矛盾数值应拒绝或标明冲突，不能最后写入覆盖。未知代、零代、尾部未返回都明确计数。

GPU分阶段数据属于设备计算，不参与CPU未归属差值。CPU命令录制、GPU计算/绘制、帧包络保持独立。每个分组列有效采样数和可观测机会数，避免用较少样本冒充完整轨迹均值。

`faces`仅为同代真实捕获N；没有捕获就保留None。另列延迟实际计数及对应代；普通计时不从延迟N构造当前预算利用率。CPU `N/B`保持原语义；GPU按容量+基础槽的资源占用另列，不能标作硬预算。

## 模式一致性与逐点比较

CPU保留当前公开hash/工作量核对。CBT只核对相同执行身份、机会/相机/投影序列和每份记录合法性；返回状态说明没有证明跨GPU进程完整mesh相同。真实捕获质量绑定自身网格，不复用另一正常进程的不存在hash。

成本比只有CPU同任务、有非空真实mesh证据、同程序/输入/后端且线程数量不同才可能称并行对照。GPU主机时间不参与该加速权限。

逐点计算提取为共享纯函数，验证源/Q/相机/投影/数组/可见域。旧调用默认要求原工作负载身份，新`--allow-different-budget`显式放开预算身份，但仍比较尺度、原始源、参考/采样语义和实际矩阵。

结果列双方实际N及数量差异；无效质量仍unpaired，不用局部成功误差作比较。不同源、不同尺度、不同投影或可见域必须拒绝。

## Suite维度与规则

CPU预算和线程仍是用户明确矩阵；Classic固定1线程，DOD不重复展开无效前缀，Transactional才展开前缀和高度/翻边策略。每个被规范化/去重的维度在suite清单写明。

CBT使用面积×容量，强制D3D12，CPU线程为不适用占位1。不得因输入了多个CPU预算或前缀而重复生成相同CBT配置。容量不是预算，case中CPU比较标签只沿基准值保留并注明。

从Transactional base生成CPU/CBT时，清除`flipRecovery`、高度策略及其他不适用字段；从CBT base生成CPU时清除所有CBT参数。所有生成case仍经`resolve_case`验证，总清单保留128项上限。

## 实施步骤

1. 保存旧CPU代表analysis的处理时间与语义摘要；保存CBT旧通用analysis失败类型，作为功能缺口证据，不将失败时间当优化基线。
2. 提取GPU观测和逐点原语，修改统一analysis空值/设备边界，保持CPU已归约值不变。
3. 修正模式核对证据等级、质量有效性和成本比许可；用CBI已冻结的成功/无效记录检查。
4. 扩展suite与CLI，生成有限CPU/CBT混合清单，验证没有无意义组合和策略泄漏。
5. 定向测试、旧CPU前后时间与结果比较；回填事实/审查/小规划并提交。随后才进入CEI-02。

## 验证与性能快验

复用CBI-04 Peking DOD50k timing、CBT面积16 timing/visual及canyon面积8无效质量。主比较只读取保存产物，不启动性能程序，不修改结果。

离线处理先一次独立Python进程记录旧CPU读取+analysis时间和峰值内存，再相同输入新进程复测；如持续超`max(0.05ms,5%,已有波动)`才补一次定向复测。输入哈希扫描成本与分析内核成本分列，不为几微秒追查。

测试覆盖：None当前N、延迟GPU预热归属/去重/矛盾代、CPU统计不变、空hash不能授权speedup、矩阵适用维度、不同预算同域逐点比较、输入/可见域错误拒绝。只跑这些受影响Python测试，复用原C++捕获证据。

## 风险与出口

CBI-03 CPU程序回归不在本阶段修复；离线变更不能被追认为生产性能修复。CBT已有质量歧义继续保留，CEI-01不得放松几何容差。

完成条件：统一analysis能够表达CPU/CBT真实证据；suite生成有效而不重复的配置；配对与代次边界有验证；已有CPU统计保持；前后离线成本、事实和审查已归档。通用report尚未完成时明确标记，不提前宣布全面接入。

## 实施结果

新增共享GPU观测层，CBI报告复用；统一analysis支持当前N缺测、设备/预算语义、代次覆盖及无效质量。模式核对区分CPU实际结果与GPU有限契约；空hash不能获得同任务加速权限。不同目录同名运行用来源限定显示ID，原清单不变，同一路径重复输入仍拒绝。

Suite按适用维度生成，新增统一`pair`入口及显式跨预算同域比较。16项定向检查、5个真实运行混合analysis、8个质量帧（含2无效）、实际跨预算配对和9个解析配置完成。

离线CPU读取/哈希543.42→509.00ms，内核0.782→0.763ms；分组和工作量完全一致。未见明显新增成本，不因约19µs差值追加验证。无C++修改，旧CPU工程回归和质量歧义分别保留。

原始记录：`benchmark-output/experiment-infrastructure/cei-01/`。详见[事实](../../codebase/experiment_infrastructure/cei_01_shared_evidence_facts.md)与[审查](../../reviews/experiment_infrastructure/cei_01_shared_evidence_review.md)。下一阶段再接通用报告，不预先宣布总体实验完成。
