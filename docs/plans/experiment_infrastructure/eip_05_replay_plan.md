# EIP-05 通用回放与真实采集小规划

2026-09-15；完成；依[大规划](experiment_infrastructure_major_plan.md)，EIP01～04已完成

## 目标与边界

把已解析case、冻结相机、材质接成公共平台/CPU回放；timing、visual、quality、profile模式分离。算法公共接口和renderer持续状态复用，不改算法策略。新增低量帧记录不能把质量校验、截图或编码藏进正常计时。

## 文件与契约

- Extend ExperimentCase：显式merge阈值、warmup、capture stride、相机文件/身份、素材路径/身份、实际prefix/donor额度；旧输入例默认值展开。Python校验SHA，C++再次校验样本/相机的规范FNV与尺寸，标明SHA不是C++重新计算。
- Create ExperimentReplay.h/.cpp：解析到公共TerrainLodSettings/相机、校验实际输入、共用逐帧CSV与实际mesh身份；不拥有图形生命周期。
- Extend ExperimentPlatformReplay：保留资产预览，通用平台驱动另置ExperimentPlatformRun.cpp，公共renderer负责上传/渲染。Capture接口复用已完成GL/D3D12实现。
- Create ExperimentCpuReplay.cpp：公共算法工厂顺序运行，借用实际packet，不通过renderer；WSL独立可执行目标仅用于CPU诊断，不假装Windows函数计时。
- Create runner.py：独占run目录、copy输入、source/build/binary/环境/模式身份、进程控制和产物SHA；包装已有native_run，Linux进程控制沿既有FPR入口。原始case不可覆盖，resolved snapshot单独存储。
- 所有模式逐机会输出同一mesh hash与必要决策统计；hash/有限结构检查在主计时外且单列evidenceMs，仍披露它们可能影响帧间cache。
- 正常计时不读回GPU、不导出mesh/图像；visual读回与实际mesh关联，quality只导出同源mesh供离线评价，profile使用独立ROI。CPU与平台时间分栏。
- 零事务、失败、预算破坏、超限保留，状态明确；同任务B1/C8才允许叫多核加速。

## 小规模验收

1. 同一生成ridge和同一Peking历史24路线，timing/visual/quality逐帧hash一致；新Peking50k/.25/.10/scaled160明确配置，不混旧20k输入例。
2. OpenGL/D3D12各一小段实际画面、错帧/行方向/分辨率核查；D3D12不强求与GL同拓扑。
3. Classic/DOD/Transactional入口至少各一个有限实例，immutable显式配置，默认fit不改。
4. CPU公共入口一例，核对输入/结果但不与Windows墙钟混表；FPR采集连接在EIP06完成。
5. 旧Peking入口与通用同协议的状态/时间边界定向对照，前后各一独立进程；不扩正式矩阵。

完成后再进入归约/质量/图表，视觉产物必须实际查看。

## 实施结果

三组24机会的三模式身份一致；Classic及GL/DX实际画面已看，CPU独立入口通过。发现并修复内部PassEvidence误开启；修复后同程序旧入口34.6913ms/通用34.0425ms，未发现工程级回归。完整决策trace仍未暴露，按公开计数/实际mesh检查，不扩大声称。[事实](../../codebase/experiment_infrastructure/eip_05_replay_facts.md)、[审查](../../reviews/experiment_infrastructure/eip_05_replay_review.md)。
