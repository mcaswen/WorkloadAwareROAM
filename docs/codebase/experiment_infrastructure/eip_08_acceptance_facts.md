# EIP-08 交付事实

2026-09-15；[小规划](../../plans/experiment_infrastructure/eip_08_acceptance_plan.md)

FACT：suite.py.expand读取一个已验证case，以预算/线程/算法/前缀笛卡尔积生成独占输出清单，最多128项；重复维度拒绝，不执行程序。非Transactional的heightPolicy用schema规定fit占位。CLI suite转交此函数，没有引入新调度器。实际生成48case，初次跨算法继承immutable触发schema拒绝，失败目录保留；修正不适用策略后成功。

FACT：CameraSequence.cpp的Header作为读写共用列协议，全部66列严格校验。新增夹具改变中间px标签但保留数值，现被拒绝；合法往返、暂停/单步/重启仍通过。没有改变相机公式/矩阵/哈希，也没有进入每帧热路径。profile_adapter先写profile-result再finalize，后续捕获将其纳入产物清单；旧捕获保留原manifest，不篡改历史记录。

FACT：图表补线程标签，相同逐帧工作曲线只画一次并保留全部analysis运行；路线补实际Forward的XZ箭头，固定位置转向不再只显示一个点。报告无函数输入时明确未采集，不展示“当前样本不足”伪装本轮归因。输出修订后重核FigureSpec、文件哈希与本地引用。

FACT：三新case三模式96机会全部结果一致；1/8峡谷96机会同任务结果一致。全部正常进程顺序执行，没有与构建、独立质量或绘图并发。独立质量16个关键帧、Peking默认拟合反例和DX4机会补查完成，结果详见[验收报告](acceptance_results.md)。初次质量命令误用bin路径，未评价mesh，失败目录记录；改为真实tests路径后独占新目录完成，没有删掉慢样本或换输入。

使用手册位于usage.md，明确构建开关、WSL/native、已冻结输入、四模式、quality目录参数、报告与清单生成。本轮最终C++ Linux和Windows GL/DX构建通过，专项camera及5个analysis测试通过，CPU入口4机会通过。没有全量CTest或正式矩阵。

UNCERTAIN：真实浏览器/用户交互仍未验收。实际图像、关键序列、误差见证与PNG图表已查看；该事实不能替代浏览器页面布局检查。本阶段实现结束时未单独提交；此次依大规划第17节分组补交。

最终差异审计发现新相机测试曾覆盖历史同名ExperimentCameraTests.cpp；已恢复HEAD原文件，将新测试独立为ExperimentCameraSequenceTests.cpp，并只改新target归属。formal_experiment_camera与experiment_camera两目标构建/执行通过（384历史输入与新序列生命周期均保留）。没有删除或弱化旧oracle。

归档身份核查补充：旧材质Tex_Terrain_Debug_Diffuse.ppm的Windows工作文件采用CRLF头部，而旧Git对象采用LF，导致新材质清单的文件哈希在新检出环境下不匹配。现按图像二进制冻结实际已使用的文件字节；差异仅为三行PPM头部换行，像素载荷逐字节一致，不改变材质外观或运行行为。未修改历史采集清单。
