# EIP-03 架构与视觉审查

2026-09-15；完成；依据[小规划](../../plans/experiment_infrastructure/eip_03_materials_plan.md)

材质数据、GPU资源、应用命令和图像输出分层，未增加算法依赖。FrameCapture按已批准后端边界前移，EIP05不再重复实现。D3D12材质切换先上传后换出，GUI消费点位于BeginFrame前；OpenGL保护pack状态和行方向。新公共数据类型说明职责，历史shader注释随参数化更新。

已实际查看两后端四张联系图（24幅真帧），形状、方向、三预设及地表素材均可辨，无上下翻转/裁剪。保留历史暖光和色彩解释；neutral是灰色基色并非完整色彩校准。缺失文件负测及状态不变检查通过，旧外观像素一致。性能36.03→34.72ms为快验，不能称算法加速。

automatedChecks=通过；agentVisualReview=完成；userVisualReview=待用户；algorithmQualityStatus=本阶段不评价。本阶段实现结束时未单独提交；此次依大规划第17节分组补交。
