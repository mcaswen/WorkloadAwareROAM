# EIP-04 相机阶段审查

日期：2026-09-15；完成

按[小规划](../../plans/experiment_infrastructure/eip_04_camera_plan.md)核查：协议与模板分离，历史导出在benchmark层；GUI请求与持久播放游标在app层，算法仅接收公共view。未改变旧Formal A64校验，旧哈希与新哈希分别命名，不偷换身份。

专项检查发现并修正暂停游标提前显示下一姿态的问题，当前暂停保持最后实际应用的视图；单步和恢复才消费下一行。GUI尺寸不符直接停止。五组真实路线关键帧已观看，环绕/接近/横穿/转向返回/静止可辨，没有穿地，近景视口外裁切是镜头构图，不是截图裁切。保留返回后网格面数与初帧不同的真实持续行为。

automatedChecks=通过；agentVisualReview=完成；userVisualReview=待用户；algorithmQualityStatus=未在本阶段评价。成本和128行历史位一致结果见[事实](../../codebase/experiment_infrastructure/eip_04_camera_facts.md)。本阶段实现结束时未单独提交；此次依大规划第17节分组补交。

提交归档补充：冻结CSV保留原导出的CRLF字节，清单SHA/FNV包含换行；.gitattributes将该目录标为不做文本换行转换，避免Windows/Ubuntu检出改变输入身份。65条清单的暂存文件字节与原SHA逐项核对；没有重写路线或历史证据，不影响运行成本。
