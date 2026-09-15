# EIP-04 相机与播放事实

2026-09-15；[规划](../../plans/experiment_infrastructure/eip_04_camera_plan.md)

CameraSequence以66列保存序号、来源样本、名义时间、事件、视口、投影参数、Position/Forward和三组矩阵。float按max_digits10以上精度输出，规范FNV-1a位编码独立于CSV排版。新poseHash包括Forward/View，与历史A的Position/Target/Up哈希不是同一种身份；historical-audit.csv同时保留旧A poseHash，原协议未改。Peking旧入口没有该独立poseHash，记空不造值。

12资产×5模板和5个历史导出共65条路线已冻结，配方/输入/文件SHA在configs/experiments/cameras/catalog.json。historical-audit.csv中A64和orbit64共128行View/NO投影逐float位相同；NO/ZO共享姿态，投影分别保存。历史头文件只补命名空间全限定，公式、顺序、索引不变。

CameraRecipe只依赖固定HeightMap reference。每机会及相邻连段4等分检查离地余量，不宣称连续数学碰撞证明。GUI关键帧录制可删除末点、选择停留、导出配方/CSV；ExperimentCameraSession负责顺序播放。暂停不推进，单步一个机会，结束暂停在末帧，重放起点才Reset；资产切换退出路线。实际视口不同则停止播放并显示原因，不静默改投影。

五条山脊路线实际执行96/96/128/96/32机会，全部机会更新，每四机会和末帧捕获。截图联系页与7.5张/名义秒播放器位于eip-04/visual/index.html；不声称实际30FPS。所有序列继续使用同一DOD实例，返回不重建会话。已看五组关键帧及路线图。

准备成本：旧64点NO构造A0.0661ms/orbit0.0120ms；新双深度域+hash包装0.1119/0.0486ms，新增费用小于0.05ms工程门槛，不讨论微秒胜负。含资产加载的新模板生成0.0355～13.0336ms，写入+读回5.0447～21.7585ms，属于离线准备。专项测试覆盖float往返、身份篡改、暂停/单步/返回与重放生命周期。

提交归档补充：冻结CSV保留原导出的CRLF字节，清单SHA/FNV包含换行；.gitattributes将该目录标为不做文本换行转换，避免Windows/Ubuntu检出改变输入身份。65条清单的暂存文件字节与原SHA逐项核对；没有重写路线或历史证据，不影响运行成本。
