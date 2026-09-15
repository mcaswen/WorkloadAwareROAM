# EIP-05 回放和采集事实

2026-09-15；[规划](../../plans/experiment_infrastructure/eip_05_replay_plan.md)

公共ReplayInput读取复制的源样本与冻结相机，C++再次核对U16和CSV规范FNV，Python校验SHA。平台直接使用公共TerrainRenderer，CPU直接使用算法工厂。相机按机会推进，返回不Reset。四种模式的算法设置相同；profile限定Linux，由既有FPR会话控制。所有机会计时后检查真实mesh的有限性、绕序、索引和硬预算并计算全mesh身份；该证据成本单列，帧间cache影响明确披露。

frames.csv固定60列。没有Transactional的字段为空，CPU没有图形的字段为空。已有函数/阶段计时与公共事务计数直接记录，不伪造完整决策trace。截图绑定frame/pose/projection/mesh identity；visual的Present包含实际GPU读回，不能作为正常性能。quality导出实际float mesh和使用的矩阵，评价在另一个进程。

runner逐次独占输出目录，保存case、resolved、复制输入、source ZIP/清单、commit/dirty状态、构建缓存、程序SHA、CPU/OS、过程argv/日志和产物SHA。workload/task/execution身份分开，模式改变不会被误认为新算法任务。失败和超限原目录保留，原始数据在明确忽略根中。

证据根：benchmark-output/experiment-infrastructure/eip-05。corrected内ridge/DOD、Peking/DOD、Peking/Transactional immutable各24机会的timing/visual/quality，逐帧60列格式和真实mesh/公开计数一致；三份modes.json给出检查列。Classic额外4机会，GL和DX各有实际画面；Linux CPU山脊24机会通过。没有声称跨OS必然位一致、完整decision trace一致或算法质量通过。

性能：旧/新二进制旧入口CPU均值35.1733/34.3435ms，24个mesh hash相同。初版通用入口38.2098ms暴露内部PassEvidence错误开启，已恢复默认关闭；同二进制定向对照Legacy34.6913ms、通用34.0425ms，24个hash相同。只支持未发现工程级回归，不构成性能优势统计结论。初始化、manifest归档、证据校验、截图编码分别计费。

实际看过山脊/Peking中性关键帧、Transactional返回前后、Classic的GL/DX画面；正确方向、1280×720、帧号对应。corrected影像与已查看影像逐文件核对；算法既有细节/质量差异保留，不修图。用户视觉评审待进行。
