# 开发规范

本文档约定 Parallel ROAM 的目录、命名、注释、Git、个人开发流程和 AI 辅助开发规范。所有新增代码、资源、文档和实验数据都应遵守本规范。

> 当前基线只维护 Classic 与 Data-Oriented CPU ROAM，两个图形后端都消费统一 CPU mesh 输出。

## 1. 重点关注

### 本项目重点

- 目录规范：基本原则、当前目录结构、规范要求。
- 命名规范：通用命名和资源命名。
- Git 与版本管理规范。
- Benchmark scenario 和资源规范。
- Bug 记录规范：用户确认修复完成后再记录现象、定位、debug 过程、解决方案和验证方式。
- 架构边界：避免 GUI、算法、渲染和 profiling 混在一起。

### AI 辅助开发

- 可以使用 AI 辅助开发，但使用 AI 代码时必须 code review，检查是否符合当前项目规范、是否有明显逻辑谬误、是否不符合上下文。
- 使用 AI 前建议先把本开发规范上传至 AI 平台项目文件夹或当前对话框。

### Agent 开发须知

1. 未经用户许可，不得将内容提交到 Git 仓库，包括本地提交（`git commit`）和远程推送（`git push`）。完成开发、修复或阶段验收不代表获得提交许可。
2. 提交信息只描述修改内容、行为变化和影响范围，不写“检查通过”“测试通过”等验收结论。构建、测试和检查的验证记录写入阶段规划的实现结果或架构审查文档。
3. 性能验收按第 7.3 节区分工程影响与微小波动；不得仅因几微秒的局部差值反复追加实验、追究根因或阻断后续开发。用户明确暂时关闭的问题保留证据和决定，不自行重新开启。
4. 按本次修改的影响范围和风险自主选择足够的验证，不默认运行全部现有测试或所有后端矩阵。避免冗余测试，复用仍适用的既有结果；选定检查通过后即停止，只有新修改、失败或未解决风险才追加验证，具体要求见第 5.8 节。
5. 开发验证默认采用快速、小规模的前后对照，不照搬正式论文实验的场景、线程和重复矩阵。优先完成正确性与明显性能问题筛查；完整统计重复留到正式实验，或出现具体风险后定向追加，不能为凑齐规划轮数持续运行。

## 2. 目录规范

### 2.1 基本原则

1. 资源按类型和用途分类存放，不随意堆放在项目根目录。
2. 不出现 `New Folder`、`Test`、`Temp`、`Final`、`Latest`、`最终版`、`最新版` 等无意义文件夹名称。
3. 第三方资源、项目资源、源码、文档分开管理。
4. 一个资源只放一个确定位置，不在多个目录重复散落。
5. 临时验证文件在验证结束后删除或整理归档。

### 2.2 当前目录结构

```text
ParallelROAM/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                  CMake helper modules
├── scripts/                configure、build、run 和报告生成脚本
├── tools/                  项目检查工具
├── src/                    C++ source code
│   ├── app/                Application、main loop、input、camera
│   ├── platform/           SDL2 window 和 OpenGL capability 查询
│   ├── render/             OpenGL/D3D12 backend、shader 和 terrain renderer
│   ├── terrain/            HeightMap、mesh data 和基础网格
│   ├── algorithms/         统一 LOD 接口与各算法实现
│   │   ├── classic_roam/
│   │   ├── data_oriented_roam/
│   │   ├── gpu_roam/
│   ├── gui/                ImGui layer and panels
│   └── benchmark/          algorithm benchmark 和 probe
├── assets/                 project runtime assets
│   ├── fonts/
│   ├── heightmaps/
│   ├── textures/
│   └── shaders/dx12/       D3D12 terrain shader
├── benchmark-output/       Git 忽略的 runtime benchmark 和实验输出
├── docs/parallel-roam/     当前文档、历史文档和报告资源
├── tests/                  CTest 单元与结构验证
└── third_party/            vendored third-party source or assets
```

### 2.3 规范要求

1. C++ 业务源码统一放在 `src/`。
2. Shader 统一放在 `assets/shaders/`。
3. Height Map 统一放在 `assets/heightmaps/`。
4. 固定相机路径和 benchmark 参数由 `Application`、命令行覆盖项或专用实验脚本统一管理；新增外部 scenario 格式前不要创建临时目录约定。
5. 项目主题文档统一放在 `docs/`；根目录只保留项目入口 `README.md` 和总计划 `Parallel_ROAM_Project_Plan.md`。
6. 第三方依赖统一放在 `third_party/`，并保留来源和许可证说明。
7. 新增文件时优先加入已有模块目录；确实没有合适目录时再新建，并使用有意义的模块名。
8. 不确定文件应该放在哪个文件夹时，先查本文档，仍不确定再让 AI 基于本文档给建议。
9. 项目自有 `.md` 报告使用中文编写；标题、正文、状态、限制和结论必须为中文，算法名、API、shader/pass、单位、CSV 字段和表格参数可以保留英语。自动报告生成器必须直接输出中文。

### 2.4 文档术语中英文对照

项目自有文档的标题、正文、表格、图注和报告说明统一使用下表中的中文术语。该规则优先于第 2.3 节中“pass 和表格参数可以保留英语”的一般规则。

| 英文 | 中文 |
|---|---|
| worker | 线程 |
| topology | 拓扑 |
| mark | 评分 |
| scan | 扫描 |
| candidate | 候选 |
| count | 数量 |
| triangle | 三角形 |
| emit | 提交 |
| upload | 上传 |
| boundary | 边界 |

1. 对照词出现在普通文字或组合术语中时也必须替换，例如 worker count 写为“线程数量”，candidate count 写为“候选数量”，topology hash 写为“拓扑哈希”，mesh emit 写为“网格提交”。
2. 源码标识符、类型名、函数名、枚举值、action 名、命令行参数、环境变量、CSV 字段、文件名、路径、论文标题和原样检索词不得翻译，并在正文中使用反引号标识，例如 `ErrorEvaluationWorkerCount`、`TopologyEditLog` 和 `candidateCount`。
3. 不得翻译标识符内部的单词，也不得为了满足对照表而另造中文接口名。需要解释时，先保留反引号中的原名，再在其后使用中文说明。

## 3. 命名规范

### 3.1 通用要求

1. 所有命名应见名知意，禁止模糊命名。
2. 不允许使用 `Test`、`New`、`AAA`、`Final`、`Manager2` 这类无信息量名称作为正式命名。
3. 文件名、类型名、资源名、scenario 名都应体现用途和模块归属。
4. 缩写只使用业内通用写法，如 `UI`、`ID`、`CPU`、`GPU`、`LOD`、`ROAM`、`SSBO`。

### 3.2 源码文件与类型命名

1. 主要类文件名与类名一致，例如 `HeightMap.h` / `HeightMap.cpp` 对应 `HeightMap`。
2. 类、结构体、枚举使用帕斯卡命名法，例如 `TerrainConfig`、`FrameStats`、`RenderPacket`。
3. 接口使用 `I` 前缀，例如 `ITerrainLodAlgorithm`。
4. 公共抽象类型应体现职责，不用一个类型名覆盖多个功能。
5. 文件扩展名统一使用 `.h` 和 `.cpp`；Shader 使用 `.vert`、`.frag`、`.comp`。

正确示例：

- `Application`
- `Window`
- `HeightMap`
- `TerrainRenderer`
- `AlgorithmRegistry`
- `ClassicRoamAlgorithm`
- `GpuTimerQuery`

错误示例：

- `Test`
- `GameManager2`
- `NewTerrain`
- `TempRenderer`
- `Utils2`

### 3.3 变量、函数和命名空间

1. 命名空间使用 `ParallelRoam::<Module>`，模块名与 `src/` 下目录对应。
2. 公共方法使用帕斯卡命名法，例如 `Initialize()`、`Update()`、`BuildRenderData()`。
3. 私有方法使用帕斯卡命名法，但应保持职责清晰，例如 `UpdateCamera()`、`CollectActiveLeaves()`。
4. 局部变量和参数使用驼峰命名法，例如 `deltaTime`、`cameraData`。
5. 私有成员字段使用 `_` + 驼峰命名法，例如 `_moveSpeed`、`_terrainConfig`。
6. 常量使用帕斯卡命名法，例如 `MaxWorkerCount`。
7. 宏和编译定义使用全大写蛇形命名，例如 `PARALLEL_ROAM_HAS_SDL2`。
8. 尽量避免 public 可变字段；跨类访问优先使用方法、只读属性或接口。

示例：

```cpp
namespace ParallelRoam::Terrain
{
class HeightMap
{
public:
    bool LoadFromFile(const std::filesystem::path& filePath);
    float SampleHeight(float u, float v) const;

private:
    std::vector<float> _heightValues;
    uint32_t _width = 0;
    uint32_t _height = 0;
};
}
```

### 3.4 资源命名

资源命名采用“前缀_模块_对象_用途”的方式，避免重名和混乱。

| 类型 | 前缀 | 示例 |
|---|---|---|
| Height Map | `Hm_` | `Hm_Terrain_Mountain_1025` |
| Texture | `Tex_` | `Tex_Terrain_Grass_Diffuse` |
| Material config | `Mat_` | `Mat_Terrain_DebugHeatmap` |
| Shader | `Shader_` | `Shader_Terrain_Lit` |
| Compute shader | `Comp_` | `Comp_Roam_ErrorEvaluation` |
| Benchmark scenario | `Scenario_` | `Scenario_Roam_Mountain_Flythrough` |
| Camera path | `Path_` | `Path_Benchmark_CanyonLoop` |
| CSV output | `Csv_` | `Csv_Benchmark_ClassicRoam_20260703` |
| Screenshot | `Shot_` | `Shot_Debug_LodHeatmap_Frame120` |

未列出的资源类型若无特殊情况，使用文件后缀名首字母大写作为前缀，例如 `Png_`、`Obj_`、`Json_`。

## 4. Git 与版本管理规范

1. 每次提交只做一类明确修改，避免把文档、资源、架构重构和算法修改混在一起。
2. 提交前检查 `git status`，确认没有误提交构建产物、临时文件、个人 IDE 配置。
3. 不随意重写已共享提交历史；本地临时提交整理前先确认不会丢失工作。
4. 修改公共模块前先记录影响范围。
5. 发生冲突时必须解决干净，不允许把冲突标记留在文件中。
6. 资源文件若体积较大，提交前先确认是否真的需要进入仓库。
7. 对第三方代码或资源必须保留许可证和来源链接。

推荐提交信息格式：

```text
feat/fix/update/chore: 中文信息
```

提交类型只使用下列四类：

- `feat`：新增功能、阶段能力或可运行路径
- `fix`：修复 bug、回归或错误行为
- `update`：调整已有功能、文档、参数、实验口径或架构细节
- `chore`：构建、脚本、资源整理、依赖和非功能性维护

提交信息要求：

1. 标题使用中文，格式固定为 `feat/fix/update/chore: 中文信息`，不再使用 scope。
2. 提交正文写两到三句中文，说明本次完成的阶段内容、行为变化和影响范围，不写构建、测试或检查通过等验收结论。
3. 在用户已许可提交的前提下，按明确阶段或可验证子阶段组织提交，避免多个阶段长期堆在同一个提交里。
4. 提交前统计未提交文件，确认没有构建产物、临时 CSV、个人 IDE 配置或无来源资源误入提交。

示例：

```text
feat: 接入 Classic ROAM 统一算法接口

完成阶段 2 的算法接口适配，Classic CPU ROAM 通过统一 Terrain LOD 边界输出渲染包和统计数据。
算法输出与渲染后端解耦，后续 Data-Oriented 与 GPU 版本可复用同一接口。
```

## 5. 代码原则

### 5.1 基本原则

1. 一个类只负责一类明确功能，避免“上帝类”。
2. 超长文件要拆分，逻辑堆积超过可维护范围时必须重构。
3. 核心算法、渲染表现、数据配置、GUI 控制尽量分离。
4. 优先使用项目中已有框架和系统，不重复写功能类似的系统。
5. 组合优于继承。
6. 资源所有权优先使用 RAII，避免裸 `new/delete`。
7. 性能敏感路径要能被 profiler 观测，不凭感觉优化。

### 5.2 字段与访问控制

1. 默认使用 `private`。
2. 尽量避免 public 字段直接裸露给外部修改。
3. 跨类访问优先通过方法、只读 getter 或接口。
4. 指针 ownership 必须清晰：拥有关系使用 `std::unique_ptr` 或值类型；非拥有引用使用引用、指针或明确命名。
5. 全局状态必须谨慎使用；需要全局访问时优先通过 `ApplicationContext` 或显式依赖注入。

推荐写法：

```cpp
class CameraController
{
public:
    const CameraData& GetCameraData() const;
    void Update(const InputState& input, float deltaTime);

private:
    CameraData _cameraData;
    float _moveSpeed = 8.0f;
};
```

不推荐写法：

```cpp
class CameraController
{
public:
    CameraData cameraData;
    float moveSpeed;
};
```

### 5.3 方法规范

1. 方法名清晰表达行为。
2. 一个方法最好只做一件主要事情，不在一个函数里混输入、算法更新、渲染、GUI 刷新、CSV 写入。
3. 过长方法要拆分为多个方法。
4. 重复逻辑要抽取为一个方法或小工具。
5. 对性能敏感方法，避免隐式大拷贝和不必要的内存分配。

推荐示例：

```cpp
void Application::Tick(float deltaTime)
{
    PollEvents();
    UpdateCamera(deltaTime);
    UpdateAlgorithm(deltaTime);
    RenderFrame();
}
```

### 5.4 主循环使用规范

1. 主循环只保留必须逐帧执行的逻辑。
2. 不在每帧做高开销文件扫描、shader 编译、资源重复加载。
3. 需要缓存的引用提前缓存，不每帧现找。
4. 能用事件或 dirty flag 触发的逻辑，不硬塞进每帧更新。
5. Benchmark 模式下应减少无关 debug 输出，避免污染性能数据。

### 5.5 命名空间规范

命名空间应与代码文件所属目录对应：

```text
src/app/Application.h                  -> ParallelRoam::App
src/render/TerrainRenderer.h           -> ParallelRoam::Render
src/terrain/HeightMap.h                -> ParallelRoam::Terrain
src/algorithms/classic_roam/...        -> ParallelRoam::Algorithms::ClassicRoam
```

### 5.6 编码规范

1. 所有文本文件保存为 UTF-8。
2. 源码标识符、文件名和构建脚本默认使用 ASCII；项目内业务注释优先使用中文，第三方 API 名称和固定英文术语可保留英文。
3. 头文件使用 `#pragma once`。
4. include 顺序建议为：当前头文件、项目头文件、第三方头文件、标准库头文件。
5. 不在头文件中引入不必要的重型依赖；能前向声明就前向声明。
6. 格式化风格以后通过 `.clang-format` 固化；在此之前保持现有文件局部风格一致。

### 5.7 注释规范

1. 删除 AI 代码残留的“新增”“1/2/3/4 序号”“引用某某头文件”等无意义注释。
2. 项目内业务注释使用中文，第三方库名、API 名、shader 术语等固定英文可保留英文。
3. 注释用于解释“为什么这样做”和“这里有什么约束”，不是重复代码表面意思。
4. 注释行结尾不使用中文句号，也不使用中英文逗号；短语和完整句子都优先不加行尾标点。句内应按语义使用逗号、分号等标点，不把多个意思无标点地挤在一行，也不以换行代替必要的标点。
5. 公共边界类型、复杂类、结构体和枚举使用 `/// <summary>` 与 `/// </summary>` 包裹摘要正文；需要方法摘要时也使用这一格式。
6. `struct` 不能只给字段写注释，结构体本身也要说明用途和数据流位置。
7. 项目自有 C++ 源码注释覆盖率仍需大于等于 15%，口径为注释行数除以有效逻辑代码行数；纯括号、预处理、命名空间和访问限定符不进入分母，也不允许用重复函数名、字段名或参数名的注释凑数。
8. 源码中连续 `//` 或 `///` 注释块的有效正文原则上不超过 3 行；只计算有实际含义的词句，独占一行的 `<summary>`、`</summary>` 标签及空注释行不计入。正文行数由语义决定，清楚的一行说明可以保留，不统一改成两行，也不为凑足三行扩写；背景说明要拆到具体分支、循环、数据写入或异常处理附近。
9. 源码注释不写当前开发阶段、子阶段或里程碑编号，例如“阶段 2”“3B”“3C”；需要说明历史或计划时写在文档和提交信息里。
10. 公共方法只有在参数约束、调用顺序、生命周期、失败语义或跨模块边界不显然时才写方法注释。
11. 内部临时 helper 只在语义不明显时补注释，避免为简单聚合类型制造噪音。
12. 复杂方法中的关键步骤必须用 `//` 标明原理、用途或约束，尤其是 OpenGL 生命周期、线程同步、GPU/CPU 数据同步、ROAM 拓扑提交和跨平台分支。
13. 私有短方法一般不需要注释；如果逻辑复杂，应加简短说明或拆分。
14. 注释必须随代码更新，不能留下过期解释。
15. 字段和局部变量不写变量名的中文翻译；只有单位、取值约束、所有权、生命周期或与其他字段的耦合关系不明显时才单独说明。
16. 复杂算法按流程节点写注释，优先解释输入如何变成候选、拓扑如何提交、约束如何传播、GPU pass 如何同步以及结果如何验证，不逐行解释算术和赋值。
17. 模板、框架和 API 初始化代码保持低注释密度，通常只需说明模块职责、资源所有权、关键生命周期和非默认配置原因，不为每个描述符字段或样板赋值写注释。
18. 注释覆盖率是最低门禁，不是逐文件目标；审查时优先保证复杂模块的语义密度，允许简单数据结构和薄封装只保留一两句核心用途说明。

公共类型注释示例：

```cpp
/// <summary>
/// 地形 LOD 算法输出的紧凑渲染描述
/// RenderPacket 是算法层和渲染层之间的边界
/// 算法负责填充，渲染器负责消费，GUI 不应直接修改
/// </summary>
struct RenderPacket
{
    RenderPacketMode Mode = RenderPacketMode::CpuMesh;
    uint32_t ActiveTriangleCount = 0;
};
```

公共方法注释示例，仅在返回值、失败语义或调用顺序不显然时使用：

```cpp
/// <summary>
/// 返回 false 时 errorMessage 必须给出可直接定位资源或 shader 阶段的原因
/// </summary>
bool LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage);
```

私有方法注释示例：

```cpp
// 传播强制 split，直到相邻叶子满足无裂缝深度约束
void PropagateNeighborConstraints();
```

复杂流程注释示例：

```cpp
// 先按误差构建候选快照，再串行提交拓扑变化
// 提交期间产生的 forced split 会继续进入兼容链传播
CollectCandidates();
CommitTopologyChanges();
```

框架初始化代码不为显然字段逐项注释：

```cpp
// 交换链和帧资源共享同一缓冲数量，allocator 只能在对应 fence 完成后复用
description.BufferCount = FrameCount;
frames.resize(FrameCount);
```

### 5.8 按影响选择测试

1. 实现前说明受影响的行为、关键边界和最小充分验证集合。优先复用相关测试，不为低影响的可逆修改新增机械测试，也不复制实现逻辑作为测试。
2. Agent 自主判断哪些测试足够，无须为常规测试选择逐项征求许可。验证记录简述选了什么、覆盖什么风险，以及哪些已有结果仍可复用；不以测试总数作为充分性的依据。
3. 不默认全量构建、全量 CTest、重复跑所有后端或扩大性能矩阵。共享 CPU 逻辑的局部修改可选择一个后端验证；涉及后端接口、资源生命周期、编译条件或平台行为时，再覆盖实际受影响的后端。
4. 同一版本、相同相关条件下已经完成的验证可复用；改变相关实现或条件后，应重跑受影响部分。必要正确性门禁不能省略，也不能把旧版本结果写成本版本实测。正式研究协议的独立重复在正式实验执行，不自动成为每次开发验证的工作量；用户调整验证范围时记录新范围与结论限制。
5. 所选检查通过且风险已有覆盖后停止测试。只有新修改、失败、环境变化或未解决的具体风险才扩大或重复验证，并简述原因；性能复测仍受第 7.3 节的一次复测上限约束。

## 6. 架构与模块边界规范

### 6.1 分层建议

项目开发中，尽量区分以下层：

1. 平台层：SDL2 window、OpenGL capability、filesystem 和时间等平台细节。
2. 数据层：Height Map、TerrainConfig、BenchmarkScenario、CameraPath。
3. 算法层：Classic ROAM 与 Data-Oriented ROAM 的 LOD 决策。
4. 渲染层：OpenGL/D3D12 backend、shader、buffer、texture 和 terrain renderer。
5. 表现与控制层：GUI、debug view、benchmark 控制面板。
6. 观测层：CPU/GPU profiler、stats history、CSV export。

### 6.2 规范要求

1. GUI 不直接修改算法内部节点，通过 controller、配置对象或命令触发。
2. 数据配置与运行时状态分离，不把动态状态写回原始资源。
3. 模块之间优先通过接口、事件、controller 通信，避免互相硬引用。
4. 公共系统不依赖某个具体算法实现。
5. 主循环回调只负责触发入口，复杂业务逻辑放到对应类和方法中。
6. 算法层不直接创建窗口、不直接处理平台 event、不直接绘制 GUI。
7. 渲染层不决定 LOD，只消费算法输出的 `RenderPacket`。

## 7. Benchmark 数据与资源规范

### 7.1 Benchmark scenario 规范

1. 当前 runtime benchmark 使用程序内固定的离散相机采样点，并通过 `--runtime-benchmark-*` 参数覆盖高度图、地形尺度、深度、采样点数和标签。Classic、DOD 和 GPU ROAM-like 必须按相同 `sampleIndex` 执行全部姿态；`timeSeconds` 只记录实际墙钟时间，不驱动路径。三种算法共用像素 split/merge 阈值与三角形预算；`--runtime-benchmark-split-pixels`、`--runtime-benchmark-merge-pixels` 是当前 CLI 名称，旧 `*-threshold` 名称仅作为像素参数别名。`--runtime-benchmark-distance-scale` 已移除并会直接报错。
2. 正式实验命令必须记录完整参数、构建 preset、图形后端、适配器、分辨率和 VSync 状态。
3. 输出统一写入 Git 忽略的 `benchmark-output/`；需要进入报告的聚合数据和图表应保留生成脚本与来源说明。
4. 不覆盖已经用于报告结论的原始 CSV；实验变体使用独立标签和输出目录。
5. 如果后续引入外部 scenario 文件，再单独建立 schema、版本和目录规范，不提前维护不存在的 `scenarios/` 约定。

### 7.2 资源规范

1. 源文件和正式使用文件尽量分开存放。
2. 重复资源、废弃资源、无效资源及时清理。
3. 替换旧资源时必须记录说明，不要直接覆盖但不说明。
4. 正式资源与临时占位资源应区分清楚，防止误用。
5. 资源命名和存放规范参考本文档第 2、3 节。

### 7.3 工程性能回归的实际影响门槛

2026-09-10 起按用户要求采用实际影响门槛；2026-09-13 起进一步区分开发快验与正式采样，取消开发阶段默认五进程矩阵。各小规划在改代码前引用本节；旧报告保留原规则与原结论，不用新口径改写历史数据。

1. 每个实现阶段仍保存修改前基线和修改后报告，核对同条件的输入、结果及受影响路径。开发快验优先选少量代表输入，每配置先做一组独立进程对照，仅在明显波动或具体风险下补第二组；内部预热/重复取能完成筛查的短序列，不默认套用 5+30，也不强制额外独立热身进程。复用已有充分对照时不再补跑。采样前写明规模、预计耗时和停止条件。
2. 开发快验关注明显工程影响，普通耗时差先按 `max(0.05 ms, 5% × 修改前基线统计值, 已有基线波动范围)` 筛查；没有波动估计时如实标记，不能虚构。保留逐进程结果、绝对差与相对差，少量样本不声称统计显著或稳定尾部结论。确需正式采样时另冻结独立进程数、内部重复、顺序和统计方法，独立进程仍为主要统计单位；已有五进程协议的原极差口径继续适用于对应历史报告。
3. 快验发现超过实际影响门槛的持续同向变化或明确风险，才对受影响输入定向复测一次，不扩展为完整矩阵。确认问题后记录原因和用户决定的修复归属；样本不足或波动较大时注明结论限制，不为凑齐重复数无限采样。局部几微秒变化只记录；若高频累计已使整体耗时跨过门槛，则由整体指标触发调查。内存、吞吐和资源上限使用小规划的独立预算，不套用毫秒门槛。
4. 实际影响明显的持续退化、内存/资源超限，以及正确性或工作量不同仍需处理。结果不等价时不能宣称等质量提速；局部稳定的大幅退化不能用其他场景收益抵消。
5. 本节是开发工程回归门槛，不是研究中判断执行交叉、策略胜负或结果等价的阈值。更严格的微基准仅在用户明确要求时采用，采集前固定目标、预算与停止条件。
6. 用户允许暂时关闭已知问题时，状态写为“暂时关闭（用户决定，保留未决原因）”，记录日期、允许继续范围和重新开启条件。它解除当前开发阻断，不声称已修复或证明没有退化；仅在后续出现达到本节门槛的稳定新证据或用户要求时重新开启。修复型关闭与用户接受风险的暂时关闭分别记录。

## 8. 资源导入规范

1. 导入资源时记录来源、许可证和用途。
2. 不清楚许可证的资源不要进入正式仓库。
3. 大体积资源提交前先确认是否必须纳入版本管理。
4. 贴图、Height Map 和 shader 放入对应资源目录；benchmark 参数和路径按第 7 节管理。
5. 第三方资源不混入项目自制资源目录。

## 9. 个人开发流程

### 9.1 开始前

1. 明确本次任务属于文档、构建系统、框架、算法、渲染、GUI、profiling 还是 benchmark。
2. 修改前先看对应模块文档和已有代码风格。
3. 涉及公共接口时，先确认调用方和输出数据结构。
4. 涉及 benchmark 数据时，先确认配置和相机路径是否需要保持可复现。

### 9.2 完成前

1. 使用 AI 辅助开发时，应把本规范提供给 AI，并对生成代码进行 code review。
2. 配置项、资源路径、工具说明应尽量明确，方便未来维护。
3. 不把测试代码、临时输出、硬编码实验逻辑留在正式版本里。
4. 修改公共逻辑时应在提交说明或文档中记录影响范围。
5. 所有新增业务逻辑和资源操作都在 Git 仓库中统一进行，不在本地长期游离开发。

## 10. 主要注意风险点

1. 对 AI 生成的代码不检查就直接使用。
2. 直接覆盖已稳定模块或正式 benchmark scenario。
3. Git 冲突未解决干净，把冲突痕迹留在项目中。
4. 复制 GPL 等强传染性许可证代码到项目源码中。
5. 把临时 debug 输出、临时资源、临时 benchmark 数据提交进正式目录。
6. GUI、算法、渲染、profiling 边界混乱，导致后续多算法切换困难。
7. Benchmark 没有固定配置和相机路径，导致性能数据不可复现。
8. 未经用户确认修复完成就提前写 bug log，导致记录不准确。
