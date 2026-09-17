# QPC-06I 架构与验收核查

依据[小规划](../../plans/cpu_refinement/qpc_06i_sample_continuation_plan.md)、[事实](../../codebase/cpu_refinement/qpc_06i_sample_continuation_facts.md)、[结果](../../research/cpu_refinement/qpc_06i_sample_continuation_results.md)。

## Critical

无未决项。Contains保留原成员谓词；实际插值、最小稳定身份owner、闭面贡献、投影异常和Prepare/Publish边界不变。初始化仍独立求参考，持续复用依赖源/Q/HeightScale固定；自然三条96帧及样本完整验证一致。

## Major

没有新增持久数组、缓存或公共策略。没有把参考复用扩展为错误的旧投影复用。计数复用已有owner查找并循环外汇总，不引入每样本额外地图查询。两遍owner重排未证中间异常等价，明确不实施，避免隐藏契约变化。

## Minor

局部减工量明确，完整性能未突破，不以算术次数等比例外推时间。新接口为Samples私有成员资格，职责没有泄漏到预留/渲染。编译和3项定向测试完成，未跑无关后端或全量CTest。允许按本轮授权提交并进入最终核查。
