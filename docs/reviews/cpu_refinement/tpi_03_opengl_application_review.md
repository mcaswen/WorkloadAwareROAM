# TPI-03 架构与结果审查

日期：2026-09-15，对照[规划](../../plans/cpu_refinement/tpi_03_opengl_application_plan.md)与[事实](../../codebase/cpu_refinement/tpi_03_opengl_application_facts.md)。

## Critical

无未解决生命周期阻塞。注册表只创建独立实例，renderer 不依赖具体算法头或 Pipeline。旧借用在 Build/Reset 前解除，失败时不继续绘制失效数量，上传失败可补齐最新完整 CPU 输出。真实 OpenGL 故障恢复有有限程序化证据。

## Major

已修复实验 GL 查询对旧 uploader 的性能污染，分析和慢帧保留于事实 §性能回归分析。没有通过改 DOD、删除慢帧或隐藏冷启动关闭问题。CPU 短测仍有明显波动，结论限于没有逻辑工作变化与已定位上传回归被修复。

## Minor 与后续约束

有限图形检查独立放在 benchmark，沿用既有后端接口，不进入普通交互控制流。CSV schema 更新保留旧列位置，对新算法未适用字段显式标记。普通 headless 验证不借用尚不适用于任意网格的 ROAM 结果 oracle。

自然默认八帧全部零事务，不能把暖时间当作更新性能胜利；该覆盖不足原样留到 TPI-05 的冻结主输入。新路径 GL 错误查询的真实成本仍需完整计费。D3D12 尚未完成本阶段资源验收，下一阶段继续。

src 注释覆盖 15.7%，renderer 12.2%，DOD/Classic 均 20.0%。没有新增重型公开依赖或第二份事务选择器；无需扩大本阶段测试或新增算法优化。
