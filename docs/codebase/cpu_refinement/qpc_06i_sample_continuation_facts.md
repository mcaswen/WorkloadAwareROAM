# QPC-06I 样本续接局部事实

只涉及`TransactionalSamples.h/.cpp`。

- FACT：私有Contains保留原Weights中的成员测试；Weights成功后仍计算原StoredWeights。Enumerate只调用Contains，不返回插值系数，也没有为全Q增加持久存储。
- FACT：Prepare复用Value内ReferenceHeight；Refresh/初始化Evaluate继续独立计算源高。Pipeline::SetView禁止HeightScale/源语义热改，实例重建重新初始化。
- FACT：owner更替、网格高度和每次Project顺序保留。归属比较复用原newFaces查询结果记录是否重复选择，不新增地图查找。完整公开样本与优先级更新在原位置。
- FACT：新增三个循环外合并的工作计数：`continuation_unused_weights_removed`、`continuation_reference_reuses`、`continuation_owner_reselections`，不参与决定或改变配额。
- INFERENCE：两个重复算术可省，不表示整个Samples::Prepare已接近必要成本；剩余精确成员测试、临时Values映射与邻面修复仍需按最终数据定位。
