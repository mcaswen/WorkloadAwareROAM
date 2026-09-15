# CBI-02 D3D12 持续更新与平台消费

2026-09-16；依据已批准 [大规划](cbt_2024_integration_major_plan.md)。CBI-01 已提交 `8631320`，用户授权本阶段自主实现、验证与提交。

## 问题、目标和非目标

CBT 核心和 shader 已迁入，但来源适配器仍引用旧版公共字段，当前平台仅消费 CPU packet。本阶段接通 D3D12 GPU 更新与间接绘制，保持 CPU 三算法原入口，同时建立独立参数、设备拒绝和安全生命周期。

完成定义是“可选择、真实更新、可渲染、可暂停/单步/重置/切换”；不把延迟计数当同代网格，不在本阶段得出质量或跨算法速度结论。完整质量捕获、EIP 算法配置和自然对照分别属于 CBI-03/04。

不改投影面积、模板、简化、传播、容量算法；不扩 OpenGL CBT，不建立 CPU fallback，不改原 CPU uploader。新增代码采用 DOD 的显式分支与展开排版。

## 已核对的接口事实

- 当前 `TerrainRenderer` 两后端共用头；D3D12 `.cpp` 内持有私有状态。GPU 绘制不能通过填假 CPU 网格绕过该边界。
- 当前帧循环已经在 `BeginFrame` 后调用 `UpdateForView`。来源 CBT 首次资源创建及每轮记录必须使用这一安全命令列表，不能在 renderer 初始化时提前执行。
- 来源已有独立 `D3D12ProceduralTerrainPipeline`，可按职责迁为 `D3D12CbtRenderPass`，保留逐帧 SRV、PSO、单 DRAW 间接签名和独立绘制时间戳。
- 当前后端有设备、队列、帧 fence、单槽 SRV 和立即提交；缺连续 SRV 区间、SM6.6/Int64 能力查询。来源已有对应实现，可定向移植。
- 当前材质常量比来源多一个 `MaterialParameters`。必须核对 cbuffer 位置并让 CBT VS 与当前 PS 共用同一份常量数据。
- `RequiresContinuousUpdate` 已存在，可表达静止相机仍更新；暂停/单步沿现有调度能力扩展，不引入第二套循环。

## 文件归属与依赖

| 选择 | 文件 | 职责及理由 |
|---|---|---|
| Extend | ITerrainLodAlgorithm.h、TerrainLodGpuOutput.h | 增加独立 ID、GPU mode、能力及资源一致性验证，公共头仍不引入 D3D12 |
| Extend | TerrainLodAlgorithmRegistry.h/.cpp | 名称/构建可用性与设备创建上下文；CPU 无上下文调用保持兼容 |
| Extend | GraphicsBackend.h、D3D12GraphicsBackend.h/.cpp | 纯设备能力与固定堆连续分配；不含 CBT 控制器逻辑 |
| Extend | cbt_2024/d3d12、Cbt2024Support | 只映射设置/统计区块与当前调度契约，资源所有者和 pass 顺序保持来源 |
| Wrap | render/D3D12CbtRenderPass.h/.cpp | 迁入来源独立程序化绘制消费者，拥有 PSO/SRV/query，不拥有算法缓冲 |
| Extend | TerrainRenderer.h、D3D12TerrainRenderer.cpp | 持有借用 GPU 描述，分派输出、延迟首次 Build、管理安全替换与绘制 |
| Extend | TerrainRenderer.cpp | OpenGL 明确拒绝 CBT，保持 CPU 输出路径 |
| Extend | GuiTypes/ImGuiLayer、Application/CommandLine 相关文件 | 独立 CBT 设置、模式选择、暂停/单步和设备不可用原因；不参与算法决策 |
| Extend | cmake/Cbt2024.cmake、CMakeLists.txt | 仅 D3D12+开关开启时链接 GPU 组件与绘制消费者，保持关闭构建可用 |
| Create | benchmark/cbt_2024/CbtPlatformCheck.h/.cpp | 有限平台诊断入口，验证生命周期和截图；不成为常规更新依赖 |
| Extend | CbtIntegrationContractTests.cpp、相关 CLI 测试 | GPU packet 正反契约及明确拒绝，覆盖新增公共边界 |

依赖方向：GUI/CLI → 设置 → registry factory → CBT adapter → 后端资源/命令；renderer → GPU packet → 独立 render pass。诊断调用真实平台，算法不反向依赖诊断。来源移植新增文件进入来源清单，不复制整个旧 renderer。

## 关键接口与生命周期

`TerrainLodCreationContext` 只借用 `IGraphicsBackend*`。原 CPU 工厂调用不要求设备；CBT 无设备、构建关闭或 API 不匹配时返回不可用，不能创建另一个 CPU 算法。GUI 的构建可选和设备实际可用分开提示。

`TerrainLodGpuOutput` 带四类资源句柄、各自容量/跨度、DRAW 偏移、资源代和拓扑代。packet 验证排除同时携带 CPU 输出；GPU 不要求 CPU 当前知道非零活动数。CPU 查询入口在 GPU mode 返回空，禁止把旧 `_meshData` 当 CBT 证据。

资源由算法持有，render pass 只借用。首次输出进入帧后创建；切换、Reset、容量变更和源图替换先等已提交 GPU 工作，再清借用描述和对应 SRV cache。旧资源引用必须覆盖未完成命令。初始化或绘制失败向调用方返回具体原因，不静默画上一算法。

连续描述符沿来源 first-fit 固定堆规则；每份 allocation 记录 Count，释放清空整个区间。字体和 CPU 纹理仍可申请单槽。帧内复用不释放正在使用的槽，显式资源替换与 Shutdown 才回收。

GPU 诊断保留来源异步读回。`TopologyGeneration`、classification/timing sample generation、resource generation 分开；绘制时间带自己的 sample generation，不把不同代统计相加。来源故障恢复保留，但累计恢复次数必须报告，不能作为正常成功样本隐去。

## 控制流

```text
选择/初始化 → 验证构建与设备 → 准备材质与普通 render state
BeginFrame → 首次 CBT 创建或持续 RecordFrame → 发布借用 packet
Render → 当前安全帧更新 SRV → 当前常量/材质 → ExecuteIndirect
Present → fence / 延迟诊断
暂停 → 只绘制现有代；单步 → 恰好一轮；Reset/切换 → 等待后释放旧代
```

初始化与设置应用不在帧外调用 CBT 的更新。需要改变容量/源数据时先销毁旧实例，下一合法帧独立 bootstrap。相机不动仍按来源执行更新；CPU 算法继续使用已有相机缓存。

实施扫描补充：当前 GUI 在 `RenderFrame` 内立即 ApplySettings/Reset，与 CBT 在途资源不兼容。改为只登记 GUI 设置、源图及 Reset/step 请求，在同一主循环的 Present 后统一兑现；非 GUI 的初始化/回放入口保持明确帧边界。另将公开资源代定义为跨对象重建唯一身份，避免来源对象内从 1 重计时复用旧 SRV；拓扑代仍按各资源代内部记录，不改 GPU 算法顺序。

## 实施顺序

1. 保存 CBI-01 程序作为 CPU 回归基线；补当前 D3D12 基线身份，使用已有冻结 Peking 短轨迹，不构建额外压力矩阵。
2. 公共 ID/输出/工厂、设备能力和描述符边界先编译；来源字段机械映射可用脚本，但不脚本重写注释或控制逻辑。
3. 迁入独立 render pass，接通帧内 Build 与间接绘制；核对当前材质 cbuffer、资源状态和 indirect offset。
4. 接 GUI/CLI 独立参数和暂停/单步；加入有限平台诊断，留存异常而非自动切回 CPU。
5. 运行针对性契约、设备与生命周期检查；截图实际观察纹理、线框、深度、源地形方向及更新结果。
6. 对原 DOD 做前后回放；回填事实/审查/性能和视觉结果，完成后提交，再开始 CBI-03。

## 验证、成本与停止条件

- CPU 合同检查：未知 ID/关闭构建拒绝、旧 hash 和输出验证、GPU 空/错误跨度/越界 offset/混合 CPU 输出拒绝。只扩受影响测试。
- D3D12 诊断：默认 128K、固定 test129 视图，少量持续轮与一个相机变化；检查非空实际 draw、代次前进、无故障恢复及来源 blocking 验证。阻塞只在诊断启用。
- 生命周期：暂停连续两机会代次不变；单步一代；Reset 后重新生成基础演化；切 CPU 再切回、容量切换及重复 Reset 不使用旧资源或耗尽描述符。必要时使用当前 D3D12 debug message 记录。
- 视觉：固定相机保存材质/线框截图，读取图片检查真实网格，不仅凭程序 exit code。若来源退化或异常，明确记录，不以空画面验收。
- CPU 工程性能：沿开发规范 §7.3，一组旧/新 Peking 24 机会，超过门槛才一次复测；计时不与 shader/C++ 构建并行。CBI-01 的核心数学测试可复用，不重跑四容量无关核心矩阵。
- 一般快验预计秒至数十秒；首次 C++ 构建数分钟。只对失败目标追加检查，不扩正式实验或 1M 压力循环。

若设备缺能力，记录具体缺失项并保留可审查实现；不得将 CPU 路径冒充 GPU 通过。若出现需改来源分类/传播算法的错误，保留反例并停该部分，不自行改变算法基线。描述符/接口/布局迁移错误在本规划内修复。

## 交付与核查

输出 `docs/codebase/cbt_2024/platform_integration.md`、`docs/reviews/cbt_2024/cbi_02_platform_integration_review.md`，结果和截图索引写入本节。原始日志、截图和程序放在忽略的 `benchmark-output/cbt-2024/cbi-02/`。

验收必须逐项核对文件所有者、借用失效、source diff、当前材质、GPU/CPU 时间边界与 GUI/CLI 可用性；平台 PASS 不等于质量比较 PASS。

## 实施结果

CBI-02 已完成。实际文件与接口见 [平台事实](../../codebase/cbt_2024/platform_integration.md)，审查见 [实现审查](../../reviews/cbt_2024/cbi_02_platform_integration_review.md)。CMake 两后端及三个针对性公共契约通过；真实 GPU 41 机会、GUI smoke、OpenGL 拒绝均完成。

额外发现并修复：来源 VS 的 debug 插值声明与当前 PS 不兼容，D3D12 debug 给出寄存器打包错误；对齐接口后可绘制，未改分类/几何计算。成功路径补设备 ERROR/CORRUPTION 审计。

材质/线框/转向三图已实际查看；暂停/单步代次、三次 Reset、容量/源/算法切换均通过。诊断 blocking、故障恢复为零，不拿诊断时间作 GPU 性能结果。

DOD/D3D12 前后短对照：50k、8线程、24机会，去3预热 CPU update 50.005→41.308 ms；全部24机会网格哈希/N/拆分/合并一致。未检出回归，不扩大重复。原始证据在忽略目录 `benchmark-output/cbt-2024/cbi-02/`。
