# CBT 持续平台接入事实

日期：2026-09-16。范围：CBI-02；与 `source_import.md` 合读。以下为实际代码事实，质量捕获与比较尚未实现。

## 模块与所有者

| 文件/类型 | 当前职责、调用与状态 |
|---|---|
| `TerrainLodAlgorithmRegistry` | 四个独立 ID；构建可用性与解析；CBT 创建时借用 `TerrainLodCreationContext.GraphicsBackend`，缺设备不替换成 CPU 算法 |
| `ITerrainLodAlgorithm` / `TerrainLodGpuOutput` | 公共 GPU 描述不含 D3D12 头；句柄、跨度、容量、DRAW 偏移、资源代与拓扑代；CPU/GPU 混合输出被拒绝 |
| `TerrainLodCbtTypes` | 独立面积、容量、验证和几何模式；统计放在 `Stats.Cbt`，不复用 CPU 五阶段字段 |
| `D3D12CbtTerrainLodAlgorithm` | 持有 State；State 包含来源 GPU 状态、管线和源尺度；源控制器与 shader 顺序不改 |
| `D3D12GraphicsBackend` | 设备能力查询；固定 256 槽 first-fit 连续 SRV 区间；单槽调用复用区间分配 |
| `D3D12CbtRenderPass` | 来源程序化消费者的定向迁入；持有 PSO、根签名、DRAW 签名、逐帧 SRV 与绘制 query；只借用算法缓冲 |
| `D3D12TerrainRenderer` | 帧内首次 Build、持续更新、输出分派与借用失效；保留原 CPU uploader |
| `Application` / `ImGuiLayer` | CBT 选择及独立控制；设备不可用原因；GUI 请求在 Present 后兑现 |
| `CbtPlatformCheck` | 有限诊断：真实设备、资源替换、暂停/单步、截图及 debug message；不参与正常更新 |

## 输出与状态

`GpuProceduralIndirect` 使用 ActiveIndices 把活动序号映射到物理槽位，每槽三个 `TerrainMeshVertex`。IndirectDrawState 的 Active DRAW 决定真实绘制数量；CPU 的延迟活动数不能决定是否绘制。`CurrentCpuMeshForDiagnostics()` 在 GPU 模式返回空，避免取得上一算法的 `_meshData`。

资源借用有效期为下一 Build/Reset 前。公开 `GpuResourceGeneration` 来自跨 State 实例的递增身份；`TopologyGeneration` 是当前资源代内的更新次数。SRV 缓存按资源身份失效，不能因 Reset 后拓扑代重新从 1 开始而复用旧地址。来源恢复仍保留并通过累计 `FaultRecoveryCount` 暴露；实验不能隐去恢复事件。

来源 `D3D12CbtFramePipeline` 继续拥有分类、规划、传播、占用归约、索引和几何生成。各 GPU 阶段时间与 classification 带来源采样代；render pass 的绘制时间另有代次，不能与不同代计算时间求和。无真实 GPU 网格 CPU 副本，CBI-02 不提供同代 Emax。

## 控制流与生命周期

```text
Initialize/ApplySettings → 检查构建/设备；CBT 首次 Build 延后
BeginFrame → UpdateForView → CBT RecordFrame → 借用 GPU packet
Render → 安全帧 SRV → 当前材质常量 → ExecuteIndirect
Present → 完成提交 → 兑现 GUI 的设置/源图/Reset/step 请求
```

暂停只绘制，单步恰好一次更新。Reset/容量改变/源图替换/切算法等待已提交 GPU 工作，清借用与 SRV，再释放所有者；下一帧独立 bootstrap。`ResetTerrainLodAlgorithm` 在 CBT 帧尚打开时拒绝释放；GUI 已移至 Present 后。Shutdown 等待后按 render pass、算法、后端、窗口依赖释放，允许重复调用。

`RequiresContinuousUpdate` 复用现有调度，不新增更新策略枚举。面积必须有限且正。容量 CLI 只接受 128K/256K/512K/1M，能力要求 D3D12、SM6.6、Int64 shader/typed atomics；OpenGL/关闭构建明确拒绝。当前平台分配并非通用堆管理系统，容量固定，耗尽直接返回无效区间。

## 渲染接口适配

CBT VS 增加当前 `MaterialParameters`，沿用当前 `TerrainPS`、纹理和材质。来源 debug 输出的 `nointerpolation` 与当前 PS 的寄存器打包不兼容，实际 PSO 创建失败；改成与 PS 一致的插值声明。单个三角形三个顶点的调试属性本就相同，因此不改变几何或分类。源码迁移差异和 SHA 见 `source_manifest.json`。

## 入口、配置与故障

- CMake：`PARALLEL_ROAM_CBT_2024` 加 D3D12 才定义 runtime；OpenGL 与 CPU-only 不链接 D3D12 文件。
- CLI：`--algorithm cbt`、`--cbt-area`、`--cbt-capacity`、`--cbt-validation`、`--cbt-geometry`；非法值不默认回退。
- GUI：四算法独立选择，CBT 设置独立显示；CPU 专属控制禁用；暂停、单步和 Reset 沿已有按钮。
- `--cbt-platform-check OUTPUT`：拒绝覆盖输出，启用可用的 D3D12 debug layer，成功路径也检查 ERROR/CORRUPTION。
- 初始化/能力/PSO失败给出原因，PSO 带 HRESULT；不静默保留另一算法网格。

## 验证与成本证据

原始证据：`benchmark-output/cbt-2024/cbi-02/`，Git 忽略。Windows RTX 5090 D，driver `32.0.15.9186`；实际 D3D12Core `6.2.26100.9278`，不把请求的 Agility 614 当成实际加载身份。

Linux 三项公共契约/CLI 测试通过；Windows D3D12 与 OpenGL 构建通过。`lifecycle-final` 41 个机会通过 debug 错误检查；GUI smoke 返回 0，OpenGL 选择 CBT 返回 2。材质、线框、转向截图实际查看无明显裂缝/错用资源；截图不能替代数值几何验证。

`lifecycle-linkage` 的普通视图延迟活动数为 15081，转向后为 22095；暂停两机会同代，单步只加一。三次 Reset、容量变化、源重载与切回后资源身份不同，全部无故障恢复。该诊断启用 blocking，不作为正常性能结果。

DOD/D3D12 冻结 Peking 50k、8线程、24机会，去除3预热：CPU update 50.005→41.308 ms，帧包络51.686→42.906 ms，上传1.043→0.988 ms。所有24机会哈希/N/split/merge一致。一次独立进程对照未检出回归；不把噪声或下降解释为优化收益，未触发追加复测。

## 核查与未确认项

公共边界不反向依赖诊断；CPU 工厂保持无设备兼容调用；共享生命周期改动仅将 GUI 资源替换延后至安全提交点。来源核心与 compute shader 无算法修改。

UNCERTAIN：当前未验证真实同代 GPU 网格质量、跨运行原子调度确定性、所有设备/容量运行表现。旧通用 CPU benchmark 不是 CBT 质量入口；后续使用 CBI-03 的显式 GPU 计量。平台通过只覆盖持续接入、绘制和生命周期。
