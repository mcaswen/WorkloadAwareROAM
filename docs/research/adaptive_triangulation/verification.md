# 一般三角化事务：验证账本

日期：2026-09-17。阶段按完成顺序追加，未运行内容不标通过。

## ATT-01：局部替换与原语

- 纸面证据：[局部拼接和操作嵌入](operation_embeddings.md)，包含相对接口同胚的拼接/逆映射、欧拉计数、前缀上界和不适用反例。
- 有限检查：`python3 docs/research/adaptive_triangulation/checks/check_local_replacements.py --output docs/research/adaptive_triangulation/data/att_01.json`。
- [记录](data/att_01.json)：8个正例、4个反例；完整坐标/面列表和检查器及复用几何文件SHA-256。
- 费用：检查体0.00261633秒、峰值RSS21,848,064字节；启动/导入未计入检查体时间，旧同任务基线N/A。没有生产变更，因此不重跑生产性能。
- 范围：平面有理几何与明确的竖直单射嵌入实例；全局无自交需额外条件。没有新Lean证明，原有7个文件不变。

出口：**条件几何模型与有限实例成立**，继续资源/预算层；不宣称所有局部操作都可接受。

## ATT-02：资源与预算

未开始。

## ATT-03：质量与总定理

未开始。
