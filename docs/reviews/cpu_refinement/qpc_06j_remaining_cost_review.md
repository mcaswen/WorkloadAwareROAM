# QPC-06J 架构与阶段验收

依据[小规划](../../plans/cpu_refinement/qpc_06j_remaining_cost_audit_plan.md)、[代码事实](../../codebase/cpu_refinement/qpc_06j_remaining_cost_facts.md)、[结果](../../research/cpu_refinement/qpc_06j_remaining_cost_results.md)。

## Critical

无未决实现问题。新增输出只读取既有WorkLedger。捕获与恢复诊断96机会逻辑/曲面一致；Canyon静止配对差异由直接Plan绕过空批复用解释，单列并限制允许条件，没有放松非静止结果检查。数值精度、政策、默认开关、发布边界不变。

## Major

捐赠提前认证与足迹惰性区分已纠正；移动/返回实际人口不支持通过不触达省认证，未引入串行按需机制。旧Measure仍含投影拒绝义务，未以直觉删去。有限队列各候选都有保留、撤回或不实施理由，未扩展全局查询算法/新持久状态。

报告明确perf未知祖先、Linux/Windows边界、Tracy捕获扰动和影子诊断额外工作；嵌套函数份额未相加，任务时间总和未称CPU时间。未把局部工作减少夸大为整体速度突破，未宣称源高或持续质量已通过。

## Minor

专用脚本复用已有采集分析器，不建立第二套profiler。新增数据仅可复核的JSON/CSV及压缩调用路径，原始捕获/程序仍受忽略规则管理。复用06I测试与原生时间，06J仅核查两输入perf及一输入Tracy，无冗余全量测试。日常CMake开关已恢复。可按用户逐阶段授权提交，06G～06J有限范围闭环；后续新的算法问题没有自动扩展本轮范围。
