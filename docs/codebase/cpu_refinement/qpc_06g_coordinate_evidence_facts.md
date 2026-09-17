# QPC-06G 局部代码事实

范围仅`TransactionalQualitySampleEvidence`的坐标求值；其余调用链沿06F事实。

- FACT：`CoordinateBounds`与`ExactCoordinate`先检查已有完整参考；有则复用，无则用Decode/Denominator构造两个原数值域坐标，调用原`Coordinate`。
- FACT：单样本对象仍绑定state/samples、sid、output，生命周期不跨提案；没有新字段、线程共享或持久状态。
- FACT：`ReferenceBounds/ExactReference`、投影及覆盖面遍历没有改变；完整参考次数仍由原账本按真实构造统计，坐标查询不再虚增该次数。
- FACT：逐点专项用未改`Reference<T>`计算oracle，分别核对区间端点、精确值、先坐标后高度以及反向调用。
- INFERENCE：只请求精确坐标而不请求精确高度时，省掉有理双线性插值；多少属于实际热点由自然工作计数和时间验证，不由源码行数推算。
- 边界：查询后又读取完整参考会重复两个坐标的生成；接口没有将“坐标已完成”错误解释成“高度已认证”。
