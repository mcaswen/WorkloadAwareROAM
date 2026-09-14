# GWR-03 当前代码事实

TransactionalPriorityIndex<Key> 是模板头文件，保存 optional 槽记录及每 256 槽的有序块，Prefix 以块首堆归并，Count 精确。PrepareRepair 私有构建脏块，Publish 移动；不维护祖先 top-r 摘要。

Samples 使用 ReceiverIndex 和 DonorIndex 替代 set/map。BuildOrders 建立连续 key 输入，局部 Prepare 构造按槽写集；State::VertexSlot 和 PreparedTopology::AddedIndex 分别提供旧/新点槽。DynamicReference 使用同一精确查询，独立反馈控制未变。

WorkLedger 新增 IndexBlocks、IndexSlots、IndexComparisons、IndexQueryBlocks。完整建序改善但局部修复退化已记录，不存在隐式“所有索引操作更快”的事实。
