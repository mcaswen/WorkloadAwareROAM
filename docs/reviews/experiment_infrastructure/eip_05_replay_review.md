# EIP-05 架构与验收审查

2026-09-15；完成；[事实](../../codebase/experiment_infrastructure/eip_05_replay_facts.md)

ReplayInput/FrameWriter共享数据契约，PlatformRun拥有图形生命周期，CpuReplay仅依赖公共算法工厂，Python拥有归档/进程。FrameCapture沿用两个独立后端，不将资源上传、截图编码或质量评价移入算法。新增CLI显式启用，旧入口不变。

审查发现PassEvidence在通用平台入口误开启，已经改为与CPU和历史入口相同的关闭状态；按新的程序身份重跑三组模式一致性。初始较慢数据保留，不混入最终计时。完整mesh核查在计时之外且记录evidenceMs，不能宣称对缓存完全无扰动。Material不进入CPU种子，截图序号验证后才落盘。

automatedChecks=通过；agentVisualReview=完成；userVisualReview=待用户；algorithmQualityStatus=未裁决。性能核查和局限见事实。本阶段实现结束时未单独提交；此次依大规划第17节分组补交。后续进入离线计量。
