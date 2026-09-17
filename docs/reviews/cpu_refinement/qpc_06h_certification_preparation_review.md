# QPC-06H 架构核查

依据[小规划](../../plans/cpu_refinement/qpc_06h_certification_preparation_plan.md)、[事实](../../codebase/cpu_refinement/qpc_06h_certification_preparation_facts.md)和[报告](../../research/cpu_refinement/qpc_06h_certification_preparation_results.md)。

## Critical

未放松当前成员资格、结构检查、双域证书或发布绑定。失败提示不保存旧结论；诊断继续用无提示完整路径复查。最终自然输出与前阶段一致后才闭环。

## Major

缓存候选只有一次复用，按退出条件撤回，未保留无依据的新组件。若共享完整QualityFaceEvidence，其账本指针跨调用可能悬空，故没有直接复用这类对象；最终实现不扩大所有权。重型跨根/跨帧缓存仍未验证，不借本轮局部准备改动作保证。

## Minor

最终修改留在认证的原职责内，移除试作接口、注册和测试专用缓存代码。源高结果不变时复用既有视觉/质量证据。有限快验仅支持工作削减/无确认工程退化，不将微小时间变化写成稳定显著收益。
