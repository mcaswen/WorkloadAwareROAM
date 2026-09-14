# GWR-02 当前代码事实

TransactionalState 初建从单侧边设置 VertexRecord::Boundary；IsBoundary 与 Samples::BuildOrders 读此值。TransactionalValidation 从实际面边独立复查。

TransactionalViewState 独立拥有 current/spare SampleProjection 数组。Prepare 惰性分配备用，Publish 交换；首建初始化当前，其后完整覆盖备用。Samples::_values 仅保存几何/owner，Value 合成当前逻辑样本；Prepare/Publish 局部补丁通过 Store 同步几何和当前投影。

BuildOrders 仍使用完整 set/map；Project、Priority、Fit 和数值接受规则未变。新增 WorkLedger::ViewBufferAllocations/ViewBufferBytes 记录视图数组分配。该实现仅属于 experiment/greedy_transactional_lod，未接入生产 DOD。
