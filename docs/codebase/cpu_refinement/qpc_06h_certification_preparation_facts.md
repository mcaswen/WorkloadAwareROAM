# QPC-06H 认证准备局部事实

范围：`TransactionalPointwiseQuality.cpp`，上层调用和公开签名保持06G状态。

- FACT：`Certify`先清空旧QualityProof，持有两个局部样本列表；两域完全成功后才创建ProposalQualityCertificate并复制Owner/Version/Config及全部原几何绑定。
- FACT：结构和接口仍先检查，面证据仍在当前域拥有。源高E/F/H核心提示通过逐支持面`std::find`核对当前成员，再走原Recheck。未拒绝时才收集、排序完整候选，公开域仍用BoxCandidates。
- FACT：增加实际支持整理、排序条数和证书创建计数；这些不影响算法决定或配额。
- FACT：根内支持缓存试作已撤回，最终没有`TransactionalQualitySupport`类型、跨调用指针、共享样本缓存或新策略开关。候选源码由实验快照保留。
- INFERENCE：提示重复失败可以省完整集合排序；完整数值循环及旧面系数构造仍在，因此不能据证书分配次数下降推断认证同比下降。
- 生命周期：只读state/proposal输入到成功证书绑定之间没有生产修改；任何异常仍不能把部分证书发布到proposal。
