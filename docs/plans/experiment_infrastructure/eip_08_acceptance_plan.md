# EIP-08 完整案例与使用沉淀

2026-09-15；开发与定向验证完成；EIP01～07开发已闭环，EIP07真实浏览器验收仍待工具环境

## 目标与范围

以新增资产完整走通输入冻结→持续平台回放→实际截图/mesh→独立质量→analysis→图表/报告，沉淀可复用操作说明。基础设施正确显示算法残余即保留，不在本阶段修算法或扩大性能矩阵。

在新运行前冻结以下验收输入，不按结果替换：
- generated-ridge / overview-orbit / 96机会 / 50k / split4、merge2 / DOD8
- dem-canyon / reveal-return / 96机会 / 50k / split4、merge2 / DOD8、Transactional immutable8
- 后者同case另一个Transactional immutable1正常计时进程，核对同任务逐帧结果和有限线程成本
- 中性材质、1280×720、warmup3、captureStride8；上述三个主配置分timing/visual/quality独立运行
- Peking原pq-return24 / Transactional fit仅视觉和质量，保留历史默认策略的反例页；不借此重新修改质量契约
- D3D12更新到EIP05的关闭PassEvidence修订后，只做4机会核对；复用已有材料和捕获视觉证据

质量只评价每条新路线2/48/80/95，均对应真实导出帧；Peking2/15/16/23。采样域仍k=0，不新增质量定义。冷/暖/运动/返回分别保留，独立进程为统计单位；不报告正式置信区间或以同时间量级声称质量相同。

## 文件和职责

Reuse run_experiment、runner、quality、analysis、figures、report、既有配置和公共算法入口。Extend configs/experiments/cases：四个显式冻结完整案例；不改变旧case。Create suite.py：仅展开基准case的预算/线程/算法/前缀清单并校验，不执行矩阵、不建立新调度器。Extend CLI suite子命令。

Create docs/codebase/experiment_infrastructure/usage.md：构建、资产/来源、相机、case、四模式、独立质量、图表/报告、失败恢复与归档命令。Create acceptance_results.md：按资产/材质/路线/捕获/计量/图表列视觉与数值验收，并链接真实产物。每个阶段事实/审查统一索引。既有核心文件只做确有缺陷的输入校验与证据清单修复，保持依赖边界。

## 验收与停止

1. 主配置三模式逐帧实际mesh/hash/公开工作计数相同；1/8线程同任务核对。失败/超时保留，不换输入追正例
2. 实际查看新地形完整序列联系图、关键原图、见证裁剪和标准图；画面问题与算法质量问题分开
3. 图表/报告数字、来源SHA、本地链接和播放器逻辑核查；实际浏览器仍不可用则明确未完成，不代签用户视觉
4. EIP08开始/结束同Windows GL Peking DOD24正常短回放各一进程，筛查基础设施回归；没有C++行为改动时复用同一程序，不为了文档强制重编译
5. 只运行本轮接口/输入/文档检查，复用EIP01～07已充分逻辑证据；D3D12只验证实际待更新改动
6. 自动采集预算30分钟；单运行180秒/8GiB，质量单mesh180秒。构建、离线出图和视觉观看分列。遇基础设施错帧/预算/输入身份问题停止对应路径修复；既有质量残余只记录

按当前任务逐阶段提交授权，完成必要验证、文档和审查后提交。最终分别列automatedChecks、agentVisualReview、browserVisualReview、userVisualReview、algorithmQualityStatus，不使用无类型PASS。


## 实现结果与核查

新增4个96机会case；三个主case各3模式完成，逐帧实际mesh及公开计数一致；峡谷1/8同任务一致。171.57秒主自动采集，另有限DX补查；48项矩阵只生成。16个质量关键帧与逐点Dmax完成。Peking默认拟合反例复现，新峡谷质量残余完整记录，不修算法。

最后输入审计补全部CSV列名校验，共用Header保持原导出格式；profile-result先写后归档；两后端与Linux目标重建、camera专项和5个analysis测试、CPU4机会完成。主报告149引用/21FigureSpec/3播放器逻辑，反例报告56引用/7FigureSpec/1播放器逻辑检查通过；实际关键帧、图表、朝向箭头与见证裁剪已看。

同Windows GL Peking DOD24暖成本33.9657→34.1559ms，约0.56%，不追加微小差值实验。绘图/报告另计。真实浏览器与用户视觉仍待验收，不作为已通过项。本阶段实现结束时未单独提交；此次依大规划第17节分组补交。

[实现事实](../../codebase/experiment_infrastructure/eip_08_acceptance_facts.md)、[验收报告](../../codebase/experiment_infrastructure/acceptance_results.md)、[架构审查](../../reviews/experiment_infrastructure/eip_08_acceptance_review.md)已对照。

最终测试职责审计：恢复历史ExperimentCameraTests.cpp，新序列测试独立为ExperimentCameraSequenceTests.cpp；两受影响camera目标通过，没有跑全部CTest。

归档身份核查补充：旧材质Tex_Terrain_Debug_Diffuse.ppm的Windows工作文件采用CRLF头部，而旧Git对象采用LF，导致新材质清单的文件哈希在新检出环境下不匹配。现按图像二进制冻结实际已使用的文件字节；差异仅为三行PPM头部换行，像素载荷逐字节一致，不改变材质外观或运行行为。未修改历史采集清单。
