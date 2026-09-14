# TPI-05：持续平台回放与性能空间

2026-09-15，前版 `4b66cb6`。依照已批准 [Major Plan](transactional_platform_integration_plan.md)，本阶段自主闭环，不修改算法策略。

## 冻结任务与输入

已读开发/规划/事实/审查规范、TPI-01～04、SVE 相机与参数、两个 renderer、公共统计、现有 MeshQualityEvaluator。主后端固定原生 Windows OpenGL/MSVC；D3D12 为次后端。

- Peking：547²、80/12、depth20、B50000、split/merge .25/.10、r=m160；相机沿既有 budget-orbit float 公式。
- test129：129²、30/4、depth14、B4096、split/merge4/2、r=m64；相机沿既有 A 轨迹。
- 两者统一 1280×720、60°、near .1/far500。24 个机会使用 sampleIndex `[14,14,14,15,16,18,22,26,30,34,38,42,46,50,54,58,14,14,14,14,14,14,14,14]`；0 冷态，1/2 静止，3～15 移动/转向，16 返回，17～23 静止恢复。每机会显式请求一次更新，DOD 不因普通交互的位移阈值跳过。
- 主后端每输入 B1/C8/DOD8 各一个正常进程；Classic 只跑 Peking。次后端 Peking C8/DOD8 各一个进程，使用序列前八个机会。不得事后增加前缀、样本或收敛轮数。200k 不再运行，复用 SVE 已登记压力成本并明确非原生平台新证据。
- 每算法从空实例独立冷启动；renderer 的旧初始化会构造默认视图状态，记录 setup 后显式 Reset，首个冻结视图从干净状态开始。此历史平台初始化费用单列，不伪装成最优冷启动。新算法一次当前视图 DOD seed，不沿历史预跑至14。

## 文件、职责与依赖

1. Create `src/benchmark/TransactionalPlatformReplay.h/.cpp`：有限协议回放，复用 Window/GraphicsBackend/TerrainRenderer、FormalCamera 和公开 CSV；记录实际 CPU-ready、上传、BeginFrame 等待、Render/Present 和帧包络。无 GUI，明确这不是完整交互应用帧；不创建 profiler。
2. Extend `ApplicationCommandLine`/`main`/CMake：显式 `--transactional-platform-replay <case> <algorithm> <workers> <output> <normal|export>` 路由；有限值校验，不影响默认入口。Expose renderer 只读当前 CPU 输出用于诊断，借用止于下一次更新/Reset。
3. Create `src/experiment/mesh_quality/PlatformMeshArtifact.h`：诊断二进制协议，只保存实际 float 顶点字段/索引、相机和尺度。输出和读取复用协议，禁止原始结构 padding 序列化；不进入运行核心。
4. Extend 现有 `MeshQualityEvaluator`：追加显式 ZO、可选逐点误差输出与可见参数域等样本权 RMS；保持旧默认与采样顺序。Create `tests/TransactionalPlatformQualityProbe.cpp`，独立可执行文件读取实际输出与原始高度图，复用 evaluator，不把它链接正常应用。
5. Create `scripts/run_transactional_platform.py`：冻结矩阵和独立进程/超时/原始身份；Create `scripts/transactional_platform_report.py`：正常/诊断逐帧身份核对、B/C 输出比较、分组成本与质量、敏感性归约，不修改算法。

normal 与 export 各自独立回放；normal 每帧在计时包络结束后计算输出身份，export 在相同位置做同样核对，关键帧 0/2/15/16/23 写实际网格。哈希会影响帧间缓存/CPU-GPU 重叠，因此报告限定为带帧间诊断空隙的有限回放，不据此称无探针真实刷新率。不启用全量 PassEvidence、topology validation、Tracy 或配对尾扫。

## 质量与正确性

主后端 C8/DOD8 各输入独立 export；B1 与 C8 正常逐帧几何身份、计数及决策摘要相同后才复用 C8 质量。Classic 主点也导出。关键帧以 raw uint16 双线性 reference、规则采样模板 k0 评价；模板和采样独立于被测算法。采用已有 evaluator 的 UV 对应，插值实际 float xyz，包含输出舍入的几何偏移；Q 顺序与 core 分组不同，不能将其 sample ID 混用。

报告 sampled Emax、可见参数域样本 RMS（不是屏幕面积加权）、全域 Hmax、逐点 excess Dmax 和见证。缺失/歧义/跨近面不得丢点后宣称有效。返回帧16与23对比帧2同视图；未恢复报告截尾，不追加轮数。预算、有限位置/索引/面方向和渲染字节核查；实际事务不足则记录覆盖不足。采样不升级连续误差或感知保证。

## 验证、性能与退出

扩展 evaluator 的 NO/ZO、解析 RMS、逐点误差/不可见和异常保留定向测试，运行这一项和受影响 CLI 项即可。离线工具只编译所需目标；两个原生应用定向构建。正常采集总20分钟、单进程180秒、RSS8GiB；离线评价每关键帧120秒/最多4千万样本，构建与离线时间另计。

本阶段不改核心热路径；新增代码在显式诊断入口，普通平台仍用原算法/资源实现，复用 TPI-04 回归基线。本阶段正常 B1/C8/DOD 提供集成后性能记录；不为未修改热路径再跑一份冗余前后矩阵。

最终归档事实/审查/研究结果，给出 CPU-ready/上传/等待/帧包络、冷/静/动分层、实际工作与质量、已知复杂度和可避免成本。以某阶段减半/免费给出固定其他项敏感性，绝不作为可达预测。分别标记接入、持续交易、质量和性能；已完成可测量交付后提交，不自动启动新优化。

## 实现结果

已完成冻结有限回放、实际float输出评价、性能/复杂度/剩余空间分析；见[事实](../../codebase/cpu_refinement/tpi_05_platform_evidence_facts.md)、[结果](../../research/cpu_refinement/tpi_platform_results_and_headroom.md)与[审查](../../reviews/cpu_refinement/tpi_05_platform_evidence_review.md)。接入与同任务多核信号成立，Peking连续质量未恢复，不能据此宣称同质量优于Legacy。

### 诊断入口核查修正

首次 run-01 在核查器中错误地把 Classic 的上向索引绕序套到新核心。新核心参数域 CCW 对应 x/z 下向索引，法线单独朝上，两后端均不启用背面剔除。修正为分别检查既有约定，不改任何 mesh、策略或阈值。首轮数据原样保存。Windows 启动器也补上提前保留进程 handle，避免结束后 ExitCode 为 null。修正后的同协议运行另存 run-02，旧算法重测仅为可审计同版采集，不删除原始记录。
