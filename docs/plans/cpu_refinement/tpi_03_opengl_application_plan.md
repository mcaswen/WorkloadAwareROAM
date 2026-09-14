# TPI-03：应用选择、持续调度与 OpenGL 消费

> 2026-09-15；前阶段 `2341710`；依据已批准的 [TPI 大规划](transactional_platform_integration_plan.md)。本阶段自主实施，完成后单独提交。

## 目标与事实

把公共适配接入应用和既有 CPU 网格消费者。已核对开发/规划规范、TPI-02 事实，以及 renderer 的工厂、静止跳过、reset、范围上传、GUI 状态、应用命令行/实验状态机与 headless 工厂。现有三处工厂仅识别 Classic/DOD；应用的旧静止跳过会阻止事务下一批；算法失败会直接返回而不消费其最后完整网格；资源失败后不能假设已经消费的 Pending 会重现。

## 文件归属与关键选择

- Create `algorithms/TerrainLodAlgorithmRegistry.h/.cpp`：轻量名称/可用性/能力查询和统一创建，关闭开关时显式不可用；旧枚举编号和默认 all 两算法保持。
- Extend `render/TerrainRenderer.h/.cpp`：搬运事务设置，能力驱动持续更新，零 drawable 暂停，显式暂停/单步，重置前先清借用；独立的上传恢复标志。OpenGL 原有 buffer/range 操作保留，错误后下次同步最新完整 CPU mesh，不重建算法求恢复。
- Extend `gui/ImGuiLayer.h/.cpp`、`app/Application.cpp`：仅值配置、暂停/单步/重置请求与事务统计展示。旧五阶段 UI 对新算法标为不适用，不填虚假线程利用率。
- Extend `app/RuntimeBenchmarkConfig.h`、`ApplicationCommandLine.cpp`、`RuntimeBenchmark.cpp`：显式名单和事务参数、阶段字段；旧默认双算法/上传配对不变。有限接入实验可记录新算法，严格五阶段证据不接受它。
- Extend `benchmark/TerrainLodBenchmark*`：共用工厂和可用性；普通 smoke 接收第三算法，旧 strict/pair profile 显式拒绝。
- Extend CMake/预设：完整 app 链接适配，tests=OFF 可构建；注册表元数据与工厂分离编译选项，解析测试不必拖入全部算法。新实验预设要求显式 Boost 路径，不改旧默认。
- 测试沿用公共输入/范围证据，新增注册/CLI/调度定向覆盖。最终交互图形测试用原生 Windows OpenGL，由 Ubuntu 调起本机编译与进程；没有原生环境时记录具体阻塞，不以 WSL 数字替代。

定向图形检查归属 `benchmark/TransactionalPlatformProbe.h/.cpp`，通过显式 `--transactional-platform-check` 调用现有公共 renderer/backend/window，正常帧不执行。它独立负责有限检查序列与结果记录，不加入算法内部探针；TPI-04 可复用同一入口。公共 renderer 增加请求全量同步方法，恢复仍由消费者持有。CSV 沿用 `TerrainLodExperimentCsv`，schema 5 为事务字段追加有限列，旧阶段不适用值明确写 `n/a`，原有列次序保留。

注册/解析不持有算法状态；renderer 不访问 Pipeline；共享接口只增加必要的输出恢复/重试能力，保持旧实现默认行为。D3D12 本阶段仅适配共用声明与注册，不把其资源验收提前宣称完成，资源轮转放在 TPI-04。

## 实施与验收

1. 保存原生同配置旧 app 基线，再冻结第三算法选择/参数与暂停接口。初始化可推迟到首个有效渲染视图，避免用默认单位矩阵建立新算法。
2. 接入工厂、配置/CLI/UI、持续调度与有限统计；静止不跳过新算法，每次机会至多一批，暂停后的单步只消费一个请求。
3. 核查 OpenGL 数量/容量/脏范围、无字节视图帧、失败后全量恢复、切换/重载/预算降低。恢复只同步最新数据，不删除核心状态。
4. 必要解析/注册测试、完整原生编译与短运行。旧 DOD 改前/后同设置各一次短轨迹；新算法单独记录 CPU/上传/帧，不把不同任务比值称为并行收益。每进程 180 秒、采集首限 10 分钟，不扩大 SVE 矩阵。
5. 更新事实/审查/结果并提交，随后进入 TPI-04。若普通相机触发既有数值拒绝，保留失败与可恢复网格，不能改认证或种子协议追求通过。

## 实现结果

已实现统一工厂/可用性、显式算法与事务配置、静止持续更新和暂停/单步、延迟有效视图初始化、借用失效处理、OpenGL 恢复标志、schema 5 与独立事务表。新增有限公共图形检查入口；没有扩展几何或改变规划语义。

原生 RelWithDebInfo/OpenGL/NVIDIA 5090 D，tests=OFF 完整 app 可构建；解析开/关与 CSV 测试完成。真实图形检查覆盖三算法切换、连续空批、暂停/单步、零 drawable、resize、降低预算、同源重载、主动全量同步与 GL 错误后恢复。新算法八帧正常应用回放成功，但该固定前缀没有实际事务，不能通过交易性能 Gate。

发现并修复实验 GL 错误查询污染旧路径的性能回归：旧 DOD 上传均值从约 0.55ms 升到 13～14ms，第二帧约 100ms，反序仍复现；将查询限定于事务实验路径后恢复 0.550ms，原始记录与分析保留。旧逻辑哈希、面数、事件和上传字节与修改前一致；CPU 短测仍波动，不宣称稳定性能等价。详见[实现事实](../../codebase/cpu_refinement/tpi_03_opengl_application_facts.md)和[审查](../../reviews/cpu_refinement/tpi_03_opengl_application_review.md)。下一步 TPI-04。
