# Workload-Aware ROAM：正式实验准备与执行计划

本文档把研究问题、当前源码能力和正式数据采集要求整理为一套可以逐项实施和验收的流程。它回答四个问题：

1. 正式实验开始前还需要补齐哪些入口和参数
2. 每一轮使用哪些策略、基准和工作负载
3. 每一轮测量什么、输出什么、满足什么条件才能继续
4. 如何从配对数据估计性能交叉，而不是把当前经验阈值当成实验结论

**状态：准备规范已冻结，正式入口尚未全部实现（2026-08-31）。**

本文档是正式执行清单。pass 边界和合法策略仍以[具体问题定义](19-workload-aware-problem-definition.md)与[策略定义](20-workload-aware-strategy-definition.md)为准，场景规模和统计规则仍以[Pass 与 Crossover 实验说明](23-pass-crossover-experiment-specification.md)为准。若三份文档出现执行细节冲突，以本文档中经过源码核对的准备状态和入口要求为准。

## 1. 当前结论

**当前已经具备工程验收入口，但还不具备一键生成正式实验数据的完整入口。**

已经可用：

- 五个 CPU pass 可以从同一冻结输入复制状态并配对执行
- 两策略使用 AB/BA 顺序，三策略网格提交轮换六种顺序
- OpenGL 和 D3D12 可以在同一 CPU 网格数据包上配对执行两种上传方式
- 请求方式、实际方式、回退原因、线程数量、结果哈希和正确性结果已经进入 CSV
- 默认路径、压力路径和 21 项 CTest 已通过

正式采样前仍需补齐：

- 18 个场景单元和三条 64 点轨迹的确定性生成入口
- 工作负载发现、目标状态选择和目标清单读取入口
- 评分与网格提交的实验专用并行下限覆盖
- 应用级三角形预算、正式轨迹、指定上传数据包和算法集合参数
- 运行编号、源码版本、硬件环境、配置哈希和恢复执行元数据
- 10000 次 bootstrap、平局判定、性能交叉区间和阈值验证脚本

现有 `pass-crossover-replay`、`pass-crossover-stress-replay` 以及上传配对 CSV 属于入口验收数据，不属于论文正式数据。

### 1.1 准备度总表

| 能力 | 状态 | 正式运行前动作 |
| --- | --- | --- |
| 策略语义和结果等价检查 | 已具备 | 保持回归测试 |
| 同一冻结输入的 CPU 配对 | 已具备 | 接入正式场景和目标清单 |
| 同一网格数据包的上传配对 | 已具备 | 改为精确选择目标数据包 |
| 同一上传前缓冲区内容的严格配对 | 未具备 | 使用相同旧缓冲区快照或独立实验缓冲区 |
| 拓扑结果等价重放 | 已具备 | 保留为正确性回归 |
| 拓扑生产串行性能基准 | 未具备 | 串行计时分支不得建立或消费并行辅助分块 |
| 18 个场景和 A/B/C 轨迹 | 未接入 | 建立公共生成器和场景清单 |
| 评分、网格提交的无截断并行尝试 | 未具备 | 把固定 256 迁移为策略字段 |
| 工作负载发现和目标选择 | 未具备 | 实现确定性选择器 |
| 正式运行元数据和中断恢复 | 未具备 | 实现运行清单和调度脚本 |
| bootstrap、平局和性能交叉分析 | 未具备 | 实现并测试分析脚本 |

### 1.2 对应源码

| 能力 | 当前源码 |
| --- | --- |
| 阶段策略、回退和默认拓扑门槛 | `src/algorithms/TerrainLodPassTrace.h` |
| 评分线程选择和固定 256 门槛 | `src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp` |
| 网格提交线程选择和固定 256 门槛 | `src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp` |
| 冻结 CPU pass 配对 | `src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.cpp` |
| 拓扑冻结重放和安全回退 | `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp` |
| 无窗口参数和阶段 CSV | `src/benchmark/TerrainLodBenchmark.cpp` |
| 应用级参数和逐帧报告 | `src/app/ApplicationCommandLine.cpp`、`src/app/RuntimeBenchmark.cpp` |
| OpenGL/D3D12 上传配对 | `src/render/TerrainRenderer.cpp`、`src/render/D3D12TerrainRenderer.cpp` |

### 1.3 当前拓扑配对入口为什么还不能生成论文时间

`RunDataOrientedRoamPassExperiment` 已经能从同一来源状态复制串行和并行辅助副本，也会比较拓扑、活动叶、队列成员和网格修改哈希。这部分足以验证结果语义。

但当前 `ExecuteFrozenSplitTopology` 和 `ExecuteFrozenMergeTopology` 在串行副本中也会执行 `BuildInterior*Chunks`，然后调用 `CommitInterior*ChunksSerial`，最后才进入串行收敛。生产路径的 `SerialImmediate` 不应为了配合并行实验先建立安全分块。因此现有串行 `wallMs` 包含了它原本不需要的规划与分块成本，可能把性能交叉错误地推向串行不利方向。

正式入口必须把两个目的分开：

- 正确性证据仍可在计时外建立共同候选快照和分类
- `SerialImmediate` 的计时只包含生产串行队列收敛和它实际执行的维护
- `ParallelAssisted` 的计时包含候选快照、排序、分块、并行提前提交、结果整理和串行尾部
- 串行行可以回填共同输入的内部/边界候选数量用于配对分析，但不能支付并行规划成本

修复后先证明两条生产语义的规范化结果仍然一致，再允许拓扑时间进入正式 CSV。若直接串行与并行辅助不能得到一致结果，应先修正算法语义，不能继续用“串行执行并行分块”掩盖差异。

另一个现状差异是默认 `pass-crossover-replay` 场景开启了 `EnableTopologyValidation`，压力场景则关闭。细分评分在验证开启时会在阶段调用内复制并比较活动叶顺序，外层 `wallMs` 会包含这段诊断开销。两种现有配置的评分墙钟时间因此不能直接合并。正式配对必须在策略计时副本中关闭额外证据和全局拓扑验证，停表后再计算结果哈希、队列不变量和拓扑检查；状态重建本身仍打开全部检查，但它不进入策略时间。

### 1.4 当前上传配对入口还缺少什么

现有 OpenGL 和 D3D12 上传重放会让两种上传方式读取同一个 CPU 网格数据包，并交替执行 AB/BA。这已经能测量请求方式、实际方式、字节数和回退，但两次调用顺序写入同一个当前缓冲区：第二次上传开始前，缓冲区可能已经是目标帧内容。入口也没有在每次计时后读取 GPU 缓冲区比较最终内容。

正式上传配对必须额外保存目标帧之前的完整 GPU 缓冲区内容和容量。每个策略计时前，把独立实验缓冲区恢复到同一个旧内容；恢复和验证都在计时外。两种策略随后分别把同一个 CPU 数据包应用到各自缓冲区。30 次计时完成后，每种策略再执行一次不计时上传并读取完整结果，要求顶点、索引、容量和绘制参数哈希一致。不能在两个正式计时样本之间插入 GPU 读取等待，因为它会改变下一次上传的流水线状态。

## 2. 源码中的实际策略与门槛

### 2.1 正式比较的策略

| Pass | 正式策略 | 配对结果要求 |
| --- | --- | --- |
| 合并评分 | `SerialRefresh`、`ParallelRefresh` | 相同队列成员和评分结果，建堆后规范化结果一致 |
| 细分评分 | `SerialRefresh`、`ParallelRefresh` | 同上，必须与合并评分分开统计 |
| 合并拓扑 | `SerialImmediate`、`ParallelAssisted` | 规范化拓扑、活动叶、队列和网格修改一致 |
| 细分拓扑 | `SerialImmediate`、`ParallelAssisted` | 同上，强制闭合与最终收敛仍允许串行 |
| 网格提交 | `SerialDirty`、`ParallelDirty`、`SerialFull` | 规范化网格内容一致，不要求脏区间组织相同 |
| CPU 上传 | `DirtyRange`、`FullBuffer` | 使用相同网格数据包，最终缓冲区内容和绘制规模一致 |

`Automatic` 只用于生产策略选择，不作为第七种实验策略。`ParallelFull` 尚未实现和验证，不进入第一轮正式实验。

### 2.2 当前固定门槛不是实验结论

源码当前存在以下生产保护值：

| 位置 | 当前值 | 含义 |
| --- | ---: | --- |
| 合并/细分评分 | 256 个队列条目 | 小于该值时，并行请求实际回退为串行 |
| 网格提交 | 256 个待写三角形 | 小于该值时，`ParallelDirty` 实际回退为串行 |
| 细分拓扑 | 32 个候选 | 普通生产路径的默认并行辅助下限 |
| 合并拓扑 | 160 个候选 | 普通生产路径的默认并行辅助下限 |

这些数值是现有实现的保守门槛，不是测量得到的性能交叉位置。正式实验必须满足：

- 冻结重放可以把评分、网格提交和拓扑的数量门槛显式设为 0
- 该设置只解除数量门槛，不绕过安全依赖、预算和分块约束
- 实际线程数量不足 2、非空分块不足 2 或没有安全内部候选时，仍记录真实回退
- 默认交互和普通基准测试继续使用生产保护值

因此需要把评分和网格提交中的两个 `256` 从文件内常量迁移到阶段策略配置，并分别提供：

- `MergeScoreMinParallelEntryCount`
- `SplitScoreMinParallelEntryCount`
- `MeshEmitMinParallelTriangleCount`

合并与细分评分暂时允许使用相同默认值，但必须保留独立字段，避免实验结果被迫共享一个最终门槛。

## 3. 正式实验配置

### 3.1 高度图文件和加载语义

核心实验只使用仓库内已有的两张高度图，不临时下载或替换输入。文件名、实际解码结果和文件内容必须同时核对：

| `terrainId` | 文件 | 文件格式与实际解码尺寸 | 文件大小 | SHA-256 | 在核心矩阵中的作用 |
| --- | --- | --- | ---: | --- | --- |
| `test129` | `assets/heightmaps/Hm_Terrain_Test_129.pgm` | P5、8 位灰度、129×129 | 16656 B | `88e7688c5ee298e4df16a250ae37e82c1e48ae8e66c973facfcc0eb208d6448c` | 小输入、低预算和回归可控场景 |
| `peking547` | `assets/heightmaps/Hm_Terrain_Peking_513.png` | PNG、16 位灰度、**547×547** | 599118 B | `1b084bacf08e3cb67afc3744d98cc80c6dfb7d55280d93d02a3f93294e121031` | 较大输入、高预算和高工作量场景 |

`Hm_Terrain_Peking_513.png` 是历史文件名，PNG 的 IHDR 和当前 `HeightMap` 解码结果都是 547×547。正式场景编号和报告使用 `peking547`，不能根据文件名写成 513×513。项目目前没有这张高度图的测绘来源说明，因此论文只把它描述为“较大 16 位高度图”，不把它写成经过地理标定的北京 DEM。

高度值和世界坐标严格沿用当前源码：

1. `HeightMap::LoadFromFile` 调用 `stbi_load_16(..., 1)`，强制读取单通道 16 位数据
2. 8 位 PGM 由 stb 扩展到 16 位路径，两张图都以 `sample / 65535.0F` 归一化到 `[0,1]`
3. 不执行垂直翻转；图像第一行对应 `v=0` 和世界坐标 `z=-R/2`
4. 任意 `(u,v)` 使用双线性插值，越过 `[0,1]` 的采样坐标钳制到图像边界
5. `R=TerrainSize`、`H=HeightScale` 时，世界坐标为：

~~~text
x = (u - 0.5) * R
y = height(u, v) * H
z = (v - 0.5) * R
~~~

正式运行启动时重新计算两个 SHA-256；尺寸、格式或哈希任一不匹配就终止场景，不能只写警告后继续。

### 3.2 十八个场景的完整输入表

每个场景都是“高度图、轨迹和三角形预算”的唯一组合。`train` 表示可以用于性能交叉和规则拟合，`holdout` 表示在规则冻结前不得查看策略胜负。

| `scenarioId` | 高度图 | 轨迹 | 地形尺寸 `R` | 高度缩放 `H` | 最大深度 | 细分阈值 | 合并阈值 | 三角形预算 | 数据用途 |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `test129-a-b512` | `test129` | A | 30 | 4 | 14 | 4 px | 2 px | 512 | train |
| `test129-a-b4096` | `test129` | A | 30 | 4 | 14 | 4 px | 2 px | 4096 | train |
| `test129-a-b20000` | `test129` | A | 30 | 4 | 14 | 4 px | 2 px | 20000 | train |
| `test129-b-b512` | `test129` | B | 30 | 4 | 14 | 4 px | 2 px | 512 | train |
| `test129-b-b4096` | `test129` | B | 30 | 4 | 14 | 4 px | 2 px | 4096 | train |
| `test129-b-b20000` | `test129` | B | 30 | 4 | 14 | 4 px | 2 px | 20000 | train |
| `test129-c-b512` | `test129` | C | 30 | 4 | 14 | 4 px | 2 px | 512 | holdout |
| `test129-c-b4096` | `test129` | C | 30 | 4 | 14 | 4 px | 2 px | 4096 | holdout |
| `test129-c-b20000` | `test129` | C | 30 | 4 | 14 | 4 px | 2 px | 20000 | holdout |
| `peking547-a-b20000` | `peking547` | A | 80 | 12 | 20 | 0.25 px | 0.10 px | 20000 | train |
| `peking547-a-b80000` | `peking547` | A | 80 | 12 | 20 | 0.25 px | 0.10 px | 80000 | train |
| `peking547-a-b200000` | `peking547` | A | 80 | 12 | 20 | 0.25 px | 0.10 px | 200000 | train |
| `peking547-b-b20000` | `peking547` | B | 80 | 12 | 20 | 0.25 px | 0.10 px | 20000 | train |
| `peking547-b-b80000` | `peking547` | B | 80 | 12 | 20 | 0.25 px | 0.10 px | 80000 | train |
| `peking547-b-b200000` | `peking547` | B | 80 | 12 | 20 | 0.25 px | 0.10 px | 200000 | train |
| `peking547-c-b20000` | `peking547` | C | 80 | 12 | 20 | 0.25 px | 0.10 px | 20000 | holdout |
| `peking547-c-b80000` | `peking547` | C | 80 | 12 | 20 | 0.25 px | 0.10 px | 80000 | holdout |
| `peking547-c-b200000` | `peking547` | C | 80 | 12 | 20 | 0.25 px | 0.10 px | 200000 | holdout |

每行还固定使用以下公共输入：

| 输入 | 正式值 | 说明 |
| --- | --- | --- |
| 采样编号 | `k=0..63` | 每个场景恰好 64 个离散状态 |
| 可绘制区域 | 1280×720 | 必须检查后端实际 drawable，不接受只设置逻辑窗口为 1280×720 |
| 垂直视场角 | 60° | 右手坐标系 |
| 近/远裁剪面 | 0.1 / 500 | 现有应用默认值是 0.05 / 1000，正式入口必须显式覆盖，不能沿用应用默认值 |
| 投影深度范围 | 无窗口与 OpenGL 为 `[-1,1]`，D3D12 为 `[0,1]` | 两者使用相同 FOV、宽高比和裁剪面，只转换后端深度约定 |
| 局部裂缝约束 | 开启 | `EnableLocalConstraints=true` |
| 串行线程数量 | 1 | 不使用自动线程数量 |
| 并行请求线程数量 | 8 | 参考主机至少需要 8 个逻辑处理器；实际线程数量仍单独记录 |
| 核心配对的数量下限 | 五个 CPU pass 均为 0 | 只解除数量门槛，不解除依赖和资源安全检查 |
| 正式重复 | 每策略 30 次 | 同一冻结输入的重复测量，不算独立状态 |
| 策略预热 | 每策略 5 次 | 执行相同策略代码但不写入正式 CSV |
| 目标选择 | 每个 pass、每个场景最多 8 个 | 只从 `k=1..63` 的有效工作状态中选择 |
| 选择与外层顺序种子 | `20260830` | 不参与轨迹坐标生成 |
| bootstrap 次数与种子 | 10000 / `20260831` | 分析脚本固定值 |
| 浮点计算 | C++ `float`，IEEE-754 binary32 | 相机清单以 9 位有效数字输出，保证往返解析 |

不同轮次的诊断开关也必须固定，不能为了得到更好时间临时关闭部分检查：

| 运行类型 | 固定策略 | `EnablePassEvidence` | `EnableTopologyValidation` | 时间用途 |
| --- | --- | --- | --- | --- |
| 工作负载发现 | DOD 固定串行 + 增量输出 | 开启 | 开启 | 只发现工作量，不比较性能 |
| CPU pass 配对的状态重建 | DOD 固定串行 + 增量输出 | 开启 | 开启 | 重建时间不进入 pass 时间 |
| CPU pass 的计时副本 | 当前 pass 的待比较策略 | 关闭 | 关闭 | 停表后由实验框架计算哈希并执行对应检查 |
| 拓扑计时后的验证 | 不再执行策略 | 由实验框架采集 | 强制执行 | 验证发生在 `wallMs` 停表之后 |
| 端到端计时 | 表 7.5 中的固定配置 | 关闭 | 关闭 | 避免诊断扫描污染生产路径时间 |
| 端到端正确性重放 | 与上一条完全相同 | 开启 | 开启 | 不记录性能；每次正式轨迹后立即重放一次 |

公共资源结果检查始终开启。`EnableTopologyPairEvidence` 在正式配对入口中设为 `false`，因为正式入口本身已经从冻结状态复制并执行策略，不能再递归启动内部配对。

### 3.3 三条轨迹的精确公式

三条轨迹均使用 `R=TerrainSize`，采样编号固定为整数 `k=0..63`。坐标由 C++ 公共生成器使用 `float` 计算，不使用 GUI 相机、鼠标输入、样条平滑或实际帧时间。

轨迹 A“稳定慢移”：

~~~text
t = k / 63
theta = radians(-5 + 10 * t)
position = (0.85R * sin(theta), 0.30R, 0.85R * cos(theta))
target   = (0, 0.05R, 0)
~~~

它从中心轴左侧 5° 缓慢移动到右侧 5°。64 个点都不同，用于观察小幅视点变化下的队列刷新和稀疏网格修改。

轨迹 B“接近后远离”使用下面的分段公式。因为总点数是偶数，返回段使用 32 个区间，避免 `k=31` 和 `k=32` 出现完全重复的最近点：

~~~text
if 0 <= k <= 31:
    q = k / 31
    d = 1.60 - 1.35 * q
    h = 0.55 - 0.35 * q
else:
    q = (k - 31) / 32
    d = 0.25 + 1.35 * q
    h = 0.20 + 0.35 * q

position = (0, hR, dR)
target   = (0, 0.05R, 0)
~~~

关键点为：`k=0` 在 `(0,0.55R,1.60R)`，`k=31` 在 `(0,0.20R,0.25R)`，`k=32` 在 `(0,0.2109375R,0.2921875R)`，`k=63` 回到 `(0,0.55R,1.60R)`。这条轨迹在前半段持续制造细分压力，在后半段制造合并压力。

轨迹 C“快速转向与预算重入”固定相机位置，每 8 个点切换一次观察目标：

~~~text
position = (0, 0.30R, 0.95R)
j = floor(k / 8) mod 4

j=0: target=(-0.30R, 0.05R, 0)
j=1: target=( 0.30R, 0.05R, 0)
j=2: target=(0, 0.05R, -0.30R)
j=3: target=(0, 0.05R,  0.30R)
~~~

`k=0..7`、`8..15`、`16..23`、`24..31` 分别使用四个目标，`k=32..63` 再重复一次。因此每次转向后的第一个点记录突发修改，后续七个点记录同一姿态下的收敛和稳定复用。

三条轨迹共用以下朝向和矩阵构造：

~~~text
forward = normalize(target - position)
up = (0,0,-1), if abs(dot(forward,(0,1,0))) > 0.99
up = (0,1,0),  otherwise
view = lookAtRH(position, target, up)
projection = perspectiveRH_NO(60°, 1280/720, 0.1, 500)  # 无窗口/OpenGL
projection = perspectiveRH_ZO(60°, 1280/720, 0.1, 500)  # D3D12
~~~

以下绝对坐标用于人工核对生成器，不作为另一套轨迹定义：

| 地形 | 轨迹与采样 | 相机位置 | 观察目标 |
| --- | --- | --- | --- |
| `test129` | A，`k=0` | `(-2.222471,9,25.402965)` | `(0,1.5,0)` |
| `test129` | A，`k=63` | `(2.222471,9,25.402965)` | `(0,1.5,0)` |
| `peking547` | A，`k=0` | `(-5.926591,24,67.741239)` | `(0,4,0)` |
| `peking547` | A，`k=63` | `(5.926591,24,67.741239)` | `(0,4,0)` |
| `test129` | B，`k=0 / 31 / 32 / 63` | `(0,16.5,48)` / `(0,6,7.5)` / `(0,6.328125,8.765625)` / `(0,16.5,48)` | `(0,1.5,0)` |
| `peking547` | B，`k=0 / 31 / 32 / 63` | `(0,44,128)` / `(0,16,20)` / `(0,16.875,23.375)` / `(0,44,128)` | `(0,4,0)` |
| `test129` | C，全部采样 | `(0,9,28.5)` | `(-9,1.5,0)`、`(9,1.5,0)`、`(0,1.5,-9)`、`(0,1.5,9)` |
| `peking547` | C，全部采样 | `(0,24,76)` | `(-24,4,0)`、`(24,4,0)`、`(0,4,-24)`、`(0,4,24)` |

### 3.4 相机轨迹如何生成、保存和回放

这里的“录制”不是人工驾驶相机录屏，而是把确定性公式产生的离散输入保存成可校验清单。正式流程如下：

1. 场景解析器读取 18 行场景清单
2. 公共轨迹生成器为每个场景生成 64 行相机输入，共 1152 行
3. 生成器以 `std::numeric_limits<float>::max_digits10`，即 9 位有效数字写出坐标，并对浮点位模式计算 `cameraPoseHash`
4. 清单同时保存逻辑姿态、两种投影矩阵哈希和完整 `viewInputHash`
5. 无窗口与应用级入口都只能读取这份清单；启动时重新生成并核对哈希，不一致立即失败
6. 每个采样点先设置相机，再请求一次 LOD 更新，完成统计和上传记录后才把 `k` 加一
7. `RawDeltaSeconds` 只作为系统噪声字段记录，不参与轨迹推进、插值或采样选择

轨迹没有“总时长”输入，也不把 64 点解释为固定帧率下的物理运动。实验自变量是离散更新编号；图表横轴使用 `sampleIndex`，真实累计墙钟时间只作为运行环境结果另列。

相机清单固定写入 `benchmark-output/formal/<runId>/manifests/camera-samples.csv`，字段如下：

~~~text
schemaVersion,scenarioId,terrainId,trajectoryId,sampleIndex,
positionX,positionY,positionZ,targetX,targetY,targetZ,upX,upY,upZ,
fovDegrees,nearPlane,farPlane,drawableWidth,drawableHeight,
cameraPoseHash,viewMatrixHash,projectionNoHash,projectionZoHash,viewInputHash
~~~

`k=0` 用于从两棵根三角形建立第一个完整状态，不进入冻结 pass 的目标选择。对目标 `k>0`，状态重建严格执行：

~~~text
新建 DOD pipeline
按固定串行增量策略依次执行相机点 0 .. k-1
核对上一状态哈希
把相机点 k 写入本次更新输入
在每个 pass 的真实边界分别冻结副本
从同一副本执行待比较策略
~~~

五个 CPU pass 在目标更新中的冻结位置为：

| Pass | 冻结输入 |
| --- | --- |
| 合并评分 | 写入目标相机和更新序号后、刷新 `Q_m` 前 |
| 合并拓扑 | 串行完成本次 `Q_m` 评分和建堆后、执行合并前 |
| 细分评分 | 串行完成本次合并后、刷新 `Q_s` 前 |
| 细分拓扑 | 串行完成本次 `Q_s` 评分和建堆后、执行细分前 |
| 网格提交 | 串行完成本次合并与细分后、消费拓扑修改记录前 |

端到端预热固定读取同一轨迹的 `k=0..7`，不记录结果；随后重置拓扑和路径编号，再从 `k=0` 正式执行到 `k=63`。禁止把预热后的拓扑直接作为正式起点。

### 3.5 端到端渲染输入

CPU pass 主实验不创建窗口。上传和端到端实验除场景表外还固定以下渲染输入：

| 输入 | 正式值 |
| --- | --- |
| 主后端 | OpenGL 4.1 |
| 交叉检查后端 | D3D12，只运行第 3 轮规定的 24 个上传输入 |
| 图形适配器 | NVIDIA GeForce RTX 5090 D；若后端实际选择集成显卡、虚拟显示适配器或 WARP，则终止 |
| VSync | 关闭；关闭失败则本轮无效 |
| 窗口逻辑尺寸 | 1280×720 |
| 后端可绘制尺寸 | 必须实测为 1280×720，否则终止 |
| 线框 | 关闭 |
| 调试着色 | `Lit` / 0 |
| 调试叠加强度 | 0.85，不启用调试着色时不影响图像 |
| 地表纹理 | `assets/textures/Tex_Terrain_Debug_Diffuse.ppm`，P6、64×64、SHA-256 `c83a42561a752ee6092c74674cf866412dce4b3cc8b8552e8e0ef45f84e37091` |
| 光照方向 | `(-0.45,-1.0,-0.35)` |
| 光照颜色 | `(1.0,0.96,0.88)` |
| 环境光/漫反射/高光 | `0.28 / 0.85 / 0.18` |
| 人工输入 | 禁用鼠标和键盘对相机的修改 |

上传配对计时只包围 CPU 网格数据包到 GPU 缓冲区的指定上传方式，不包含绘制、呈现、相机更新和算法构建。端到端帧时间则保留算法更新、上传和渲染的独立字段，不能把三者相加后再与已经包含它们的外层墙钟时间相加。

### 3.6 参考运行环境

计划使用当前开发机作为第一台参考主机。2026-08-31 的准备快照为：

| 项目 | 当前参考值 |
| --- | --- |
| CPU | AMD Ryzen 9 9950X3D，16 核、32 逻辑处理器 |
| 内存 | 125.56 GiB 可见物理内存 |
| GPU | NVIDIA GeForce RTX 5090 D；正式报告从实际图形后端再次读取适配器名称 |
| NVIDIA 驱动 | WMI 版本 `32.0.15.9186`，对应 591.86；正式运行再次读取 |
| 操作系统 | Windows 11 专业版，10.0.26200，64 位 |
| 电源计划 | Windows“平衡”，GUID `381b4222-f694-41f0-9685-ff5bb260df2e` |
| 进程优先级 | Normal |
| 进程亲和性 | 不额外钉核，允许使用全部 32 个逻辑处理器 |
| 并行线程上限 | 8 |
| 构建 | Visual Studio 17 2022 生成器，`RelWithDebInfo`，当前标志 `/Zi /O2 /Ob1 /DNDEBUG` |

这张表是准备时快照，不代替运行时元数据。正式开跑时重新读取 CPU、内存、GPU、驱动、系统、电源计划、编译器完整版本和图形接口版本；任何值变化都使用新的 `runId`。正式运行期间保持工作区干净，关闭非必要前台程序，不修改源码、资产、驱动、电源计划和系统更新状态。

## 4. 实验清单文件

正式矩阵不继续把场景硬编码为两个配置名称。准备三个可审查的 CSV 清单：场景清单定义不变量，相机清单保存实际离散输入，目标清单保存工作负载发现后的冻结状态选择。

### 4.1 场景清单

固定文件：`docs/parallel-roam/formal-experiment-scenarios-v1.csv`

固定包含：

~~~text
schemaVersion
scenarioId
terrainId
heightMapPath
terrainSize
heightScale
maxDepth
splitPixels
mergePixels
triangleBudget
trajectoryId
analysisSplit
sampleCount
drawableWidth
drawableHeight
fovDegrees
nearPlane
farPlane
localConstraints
mergeScoreParallelMinimum
splitScoreParallelMinimum
splitTopologyParallelMinimum
mergeTopologyParallelMinimum
meshParallelMinimum
serialWorkerCount
parallelWorkerCount
passWarmupCount
passMeasuredRepeatCount
heightMapWidth
heightMapHeight
heightMapSha256
~~~

18 行的取值就是 3.2 节的完整输入表；公共字段使用 3.2 节公共输入表中的固定值。`scenarioId` 不再自由命名，只允许 `test129-a-b512` 到 `peking547-c-b200000` 这 18 个值。程序启动后必须把解析结果原样写回运行元数据，不能只保存名称。

### 4.2 相机输入清单

固定文件：`benchmark-output/formal/<runId>/manifests/camera-samples.csv`

字段和生成方法已经在 3.4 节固定。它必须恰好包含 1152 行数据，并满足：

- 每个 `scenarioId` 恰好存在 `sampleIndex=0..63`
- 同一地形、同一轨迹但不同预算的逻辑相机姿态相同
- `cameraPoseHash` 只包含位置、目标和 up，不包含预算
- `viewInputHash` 包含投影约定、可绘制尺寸、相机、视图矩阵和投影矩阵
- OpenGL 与 D3D12 的逻辑姿态哈希相同，投影矩阵哈希按 NO/ZO 深度约定分别保存

这份清单在第 1 轮开始前生成并冻结。后续回放读取清单中的数值，公式只用于启动时交叉校验，不能在不同入口各自重新解释轨迹。

### 4.3 目标状态清单

固定文件：`benchmark-output/formal/<runId>/manifests/target-states.csv`

固定包含：

~~~text
schemaVersion
scenarioId
passId
sampleIndex
selectionStratum
selectionRank
selectionSeed
primaryWorkValue
featureVector
replayInputHash
selectionFeatureHash
analysisSplit
~~~

`selectionStratum` 只允许：

- `low`
- `middle`
- `high`
- `coverage`

最后一类表示补齐辅助特征空间或高依赖结构，不表示已经知道性能交叉位置。

## 5. 实验入口与参数

### 5.1 当前可用的 CPU 配对入口

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm dod `
  --profile pass-crossover-replay `
  --pass-warmups 5 `
  --pass-repeats 30 `
  --pass-workers 8 `
  --pass-targets 0 `
  --csv benchmark-output\pass-crossover-default.csv
~~~

当前参数含义：

| 参数 | 当前行为 | 正式实验限制 |
| --- | --- | --- |
| `--profile` | 只能选择默认或压力硬编码场景 | 不能生成 18 个场景单元 |
| `--pass-warmups` | 控制每种策略预热次数 | 可直接复用 |
| `--pass-repeats` | 控制每种策略正式重复次数 | 可直接复用 |
| `--pass-workers` | 同时设置五个 CPU pass 的线程上限 | 主实验固定为 8，可复用 |
| `--pass-targets` | 从路径起点后顺序取前 N 个状态，0 表示全部 | 不能替代目标状态清单 |
| `--csv` | 指定单个输出文件 | 缺少运行目录、恢复执行和元数据绑定 |

### 5.2 当前可用的上传配对入口

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-upload-pair `
  --runtime-benchmark-upload-warmups 5 `
  --runtime-benchmark-upload-repeats 30 `
  --runtime-benchmark-upload-targets 24
~~~

当前入口会选择最先出现的稳定增量网格数据包。它可以验证同数据包配对，但不能按目标状态清单选择低、中、高和高碎片度数据包。

### 5.3 正式 CPU 实验需要新增的参数

以下参数为待实现接口，名称在实现前冻结：

| 参数 | 含义 |
| --- | --- |
| `--profile workload-discovery` | 运行 18 个场景中的工作负载发现，不做策略胜负判断 |
| `--profile pass-crossover-formal` | 按目标状态清单运行正式 CPU pass 配对 |
| `--profile timing-calibration` | 运行计时分辨率和短时漂移标定 |
| `--write-camera-manifest <path>` | 从场景清单生成 1152 行确定性相机输入并退出 |
| `--scenario-manifest <path>` | 读取场景清单 |
| `--camera-manifest <path>` | 读取并校验冻结的相机输入清单 |
| `--scenario-id <id\|all>` | 选择单个场景或全部场景 |
| `--target-manifest <path>` | 读取目标状态清单 |
| `--pass-id <id\|all>` | 选择单个 pass 或全部 CPU pass；允许值见下文 |
| `--calibration-repeats <n>` | 设置计时标定重复次数，正式准备默认为 1000 |
| `--experiment-run-id <id>` | 绑定一轮正式数据的稳定编号 |
| `--experiment-seed <value>` | 目标选择和外层顺序随机种子 |
| `--experiment-output-dir <path>` | 指定本轮输出根目录 |
| `--experiment-resume` | 跳过已完成且校验通过的场景单元 |
| `--merge-score-min-parallel-entries <n>` | 合并评分的实验并行数量下限 |
| `--split-score-min-parallel-entries <n>` | 细分评分的实验并行数量下限 |
| `--mesh-min-parallel-triangles <n>` | 网格提交的实验并行数量下限 |
| `--split-topology-min-candidates <n>` | 细分拓扑的并行数量下限，已有普通入口 |
| `--merge-topology-min-candidates <n>` | 合并拓扑的并行数量下限，已有普通入口 |

正式性能交叉测量把五个数量下限设为 0。生产默认值仍写入元数据，作为后续默认策略基准。

`--pass-id` 的稳定值为 `merge-score`、`split-score`、`merge-topology`、`split-topology`、`mesh` 和 `all`。CPU 上传继续使用应用级入口，不混入无窗口 CPU pass 列表。

### 5.4 正式上传与端到端实验需要新增的参数

| 参数 | 含义 |
| --- | --- |
| `--runtime-benchmark-scenario-manifest <path>` | 使用正式场景清单，而不是两个内置路径 |
| `--runtime-benchmark-camera-manifest <path>` | 逐行读取正式相机输入，不使用应用内置轨迹 |
| `--runtime-benchmark-target-manifest <path>` | 精确选择上传数据包 |
| `--runtime-benchmark-algorithm <classic\|dod\|all>` | 避免上传实验无条件运行两种算法 |
| `--runtime-benchmark-triangle-budget <n>` | 显式设置三角形预算 |
| `--runtime-benchmark-trajectory <A\|B\|C>` | 选择正式离散轨迹 |
| `--runtime-benchmark-repeat-index <n>` | 记录完整轨迹的重复编号 |
| `--runtime-benchmark-run-id <id>` | 与 CPU 数据共享运行编号 |
| `--runtime-benchmark-output-dir <path>` | 输出到本轮正式目录 |

压力路径当前会在应用启动阶段覆盖高度图、地形参数和预算。正式入口必须改为“场景清单是唯一权威输入”，禁止内置路径在解析后静默覆盖显式参数。

### 5.5 各轮完整命令形态

下面是待实现正式入口的完整命令形态。尖括号中的 `runId` 是唯一需要在开跑时生成的值，其余数值不得临时修改。

先生成并校验相机清单：

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm dod `
  --scenario-manifest docs\parallel-roam\formal-experiment-scenarios-v1.csv `
  --write-camera-manifest benchmark-output\formal\<runId>\manifests\camera-samples.csv `
  --experiment-run-id <runId> `
  --experiment-seed 20260830
~~~

运行工作负载发现：

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm dod `
  --profile workload-discovery `
  --pass-policy serial-incremental `
  --scenario-manifest docs\parallel-roam\formal-experiment-scenarios-v1.csv `
  --camera-manifest benchmark-output\formal\<runId>\manifests\camera-samples.csv `
  --scenario-id all `
  --experiment-run-id <runId> `
  --experiment-seed 20260830 `
  --experiment-output-dir benchmark-output\formal\<runId>\raw\discovery
~~~

按冻结目标运行五个 CPU pass：

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm dod `
  --profile pass-crossover-formal `
  --scenario-manifest docs\parallel-roam\formal-experiment-scenarios-v1.csv `
  --camera-manifest benchmark-output\formal\<runId>\manifests\camera-samples.csv `
  --target-manifest benchmark-output\formal\<runId>\manifests\target-states.csv `
  --scenario-id all `
  --pass-id all `
  --pass-warmups 5 `
  --pass-repeats 30 `
  --pass-workers 8 `
  --merge-score-min-parallel-entries 0 `
  --split-score-min-parallel-entries 0 `
  --merge-topology-min-candidates 0 `
  --split-topology-min-candidates 0 `
  --mesh-min-parallel-triangles 0 `
  --experiment-run-id <runId> `
  --experiment-seed 20260830 `
  --experiment-output-dir benchmark-output\formal\<runId>\raw\cpu-pass
~~~

运行 OpenGL 上传配对：

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-upload-pair `
  --runtime-benchmark-algorithm dod `
  --runtime-benchmark-scenario-manifest docs\parallel-roam\formal-experiment-scenarios-v1.csv `
  --runtime-benchmark-camera-manifest benchmark-output\formal\<runId>\manifests\camera-samples.csv `
  --runtime-benchmark-target-manifest benchmark-output\formal\<runId>\manifests\target-states.csv `
  --runtime-benchmark-upload-warmups 5 `
  --runtime-benchmark-upload-repeats 30 `
  --runtime-benchmark-upload-targets 8 `
  --runtime-benchmark-run-id <runId> `
  --runtime-benchmark-output-dir benchmark-output\formal\<runId>\raw\upload-opengl
~~~

D3D12 使用同一命令，但换成 D3D12 构建和 `upload-d3d12` 输出目录；调度器只传入 `test129-*-b4096` 与 `peking547-*-b80000` 六个中预算场景，并把每场景目标数量设为 4。

端到端固定配置不靠一条命令同时运行全部组合。调度脚本按“场景 × 轨迹重复 × 配置顺序”逐进程调用下面的单配置入口：

~~~powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-algorithm <classic|dod> `
  --runtime-benchmark-policy <serial-incremental|maximum-parallel-incremental|serial-full|maximum-parallel-full> `
  --runtime-benchmark-scenario-manifest docs\parallel-roam\formal-experiment-scenarios-v1.csv `
  --runtime-benchmark-camera-manifest benchmark-output\formal\<runId>\manifests\camera-samples.csv `
  --scenario-id <scenarioId> `
  --runtime-benchmark-samples 64 `
  --runtime-benchmark-warmup-samples 8 `
  --runtime-benchmark-repeat-index <0..4> `
  --runtime-benchmark-run-id <runId> `
  --runtime-benchmark-output-dir benchmark-output\formal\<runId>\raw\fixed-baseline
~~~

Classic 的 `--runtime-benchmark-policy` 解析后记录为 `notApplicable`，不改变 Classic 内部实现。每个子进程都把最终解析值写入元数据，脚本只负责枚举，不在命令行重新展开地形参数。

### 5.6 批量调度入口

新增 `scripts/run_formal_experiments.ps1`，只负责调度，不在脚本中重新实现算法逻辑。参数固定为：

~~~powershell
.\scripts\run_formal_experiments.ps1 `
  -Stage discovery|cpu-pair|upload|fixed-baseline|oracle|adaptive `
  -ScenarioManifest <path> `
  -TargetManifest <path> `
  -RunId <id> `
  -OutputDirectory <path> `
  -Resume
~~~

脚本职责：

- 逐场景启动独立进程
- 轮换场景和策略外层顺序
- 检查退出码、CSV 行数、配置哈希和正确性字段
- 已完成场景只在 `-Resume` 下跳过
- 不覆盖已有原始数据
- 生成本轮 `run-manifest.csv` 和失败清单

## 6. 目标状态选择

### 6.1 选择原则

每个场景单元先用 DOD 固定串行增量策略运行完整 64 点轨迹。只根据执行前或规划期间的工作负载字段选择目标状态，不读取策略耗时和胜负。

先形成每个 pass 自己的可选集合：

- 一律排除 `k=0`，因为它包含首次节点池和网格建立
- 排除输入哈希、预算、队列、资源或拓扑检查失败的状态
- 合并/细分评分要求对应队列条目数量大于 0
- 合并/细分拓扑要求冻结候选数量大于 0
- 网格提交要求活动三角形数量大于 0 且预计待写三角形数量大于 0
- CPU 上传要求不是首次上传、不扩容、没有帧槽积压、完整缓冲区字节数大于 0 且脏字节数大于 0

无工作状态仍保留在发现 CSV，用于说明某个 pass 没有运行，但不拿它估计串行与并行的性能交叉。

每个 pass 每个场景选择 8 个状态：

| 分层 | 数量 | 选择方式 |
| --- | ---: | --- |
| 低工作量 | 2 | 主要工作量排序后的下三分之一内确定性抽取 |
| 中工作量 | 2 | 主要工作量排序后的中三分之一内确定性抽取 |
| 高工作量 | 2 | 主要工作量排序后的上三分之一内确定性抽取 |
| 覆盖补点 | 2 | 在归一化辅助特征空间中最大化与前六点的距离 |

具体算法固定为：

1. 按“主要工作量、采样编号”升序排列 `n` 个可选状态，排序位置为 `r=0..n-1`
2. 用 `min(2, floor(3r/n))` 得到低、中、高三层编号
3. 对每个状态计算 UTF-8 字符串 `20260830|scenarioId|passId|sampleIndex|stratum` 的 FNV-1a 64 位散列；每层按散列值、采样编号升序取前两个
4. 若某层不足两个，缺少的名额不从相邻层强行补齐，而是交给覆盖补点步骤
5. 对辅助特征逐维计算场景内 P5 和 P95；百分位位置 `p(n-1)` 的非整数部分用线性插值
6. 用 `clamp((x-P5)/(P95-P5),0,1)` 归一化；若 `P95=P5`，该维固定为 0
7. 对剩余状态计算它到已选集合的最小欧氏距离，选择距离最大的状态；并重复到总数达到 8
8. 距离相同时先比较上面的固定散列值，再按采样编号升序

只要可选状态不少于 8，这个过程就必须产生 8 个不同采样编号。少于 8 时全部保留，并写出 `coverageInsufficient=1` 和缺少数量。同一输入、选择器版本和种子必须产生逐字节相同的目标清单。

### 6.2 各 pass 的选择字段

| Pass | 主要字段 | 覆盖补点字段 |
| --- | --- | --- |
| 合并评分 | `mergeQueueEntryCount` | 队列条目数量、`activeTriangleCount / triangleBudget`、`maxActiveDepth / maxDepth` |
| 细分评分 | `splitQueueEntryCount` | 队列条目数量、`activeTriangleCount / triangleBudget`、`remainingBudget / triangleBudget`、`maxActiveDepth / maxDepth` |
| 合并拓扑 | `interiorCandidateCount + boundaryCandidateCount` | 内部候选比例、边界候选比例、`nonEmptyChunkCount / 8`、预算余量比例 |
| 细分拓扑 | `interiorCandidateCount + boundaryCandidateCount` | 内部候选比例、边界候选比例、`nonEmptyChunkCount / 8`、预算余量比例 |
| 网格提交 | `predictedDirtyTriangleCount / activeTriangleCount` | 活动三角形预算占比、拓扑修改记录占比、`predictedDirtyRangeCount / predictedDirtyTriangleCount` |
| CPU 上传 | `dirtyBytes / fullBufferBytes` | 更新区间数量、平均区间字节数占完整缓冲区比例、完整缓冲区字节数 |

所有除法在分母为 0 时记为 0。拓扑的内部/边界数量来自对冻结候选执行的只读规划；网格的预计脏数量和预计区间来自写入前的槽位规划。这些规划耗时写入 `featureCollectionMs`，以后在线策略若使用同一特征也必须支付该成本。

选择器明确不读取本帧或上一帧的墙钟时间、评分时间、提交时间、获胜策略、提前提交数量、实际串行尾部或实际上传时间。这样目标覆盖由结构工作量决定，不会被当前机器上的一次噪声测量反向塑形。

没有足够唯一状态时，不复制同一帧凑满 8 个，而是记录 `coverageInsufficient=1` 和实际数量。

OpenGL 上传主实验按同一算法选择每场景 8 个输入。D3D12 的 4 个交叉检查输入只从对应中预算场景已冻结的 8 个上传输入中取得：低、中、高层各取 `selectionRank=0` 的一个，再从 `coverage` 中取更新区间数量最多的一个；相同时取较小采样编号。

### 6.3 防止循环选择

目标状态选择不得使用：

- 当前 `256/32/160` 生产门槛作为“真实交叉位置”
- 串行或并行实测时间
- 当前状态的获胜策略
- 实际提前提交数量、实际串行尾部时间等执行后字段

性能交叉只能在正式配对测量完成后估计。若第一轮状态在交叉区间附近过于稀疏，进入独立的边界确认轮，而不是回头改写核心目标清单。

## 7. 正式实验轮次

### 第 0 轮：源码、环境和计时基线冻结

目的：确认本次运行具备采集正式数据的资格。

执行：

- OpenGL 与 D3D12 构建
- 运行全部 CTest
- 运行默认路径和压力路径验收
- 记录 1000 次空计时和 1000 次无工作阶段计时，确认计时分辨率
- 连续运行三次 `pass-crossover-replay` 短基准，每次预热 2 个块、正式记录 10 个块
- 写出 Git commit、工作区状态、CPU/GPU/驱动、电源计划和配置哈希

验收命令至少包含：

~~~powershell
.\tools\cmake\bin\ctest.exe `
  --test-dir build\relwithdebinfo-fetch `
  -C RelWithDebInfo `
  --output-on-failure

.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-path default

.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-path budget-saturation
~~~

D3D12 构建至少运行现有 OCBT、基础拓扑和程序化绘制专项测试；正式上传入口完成后再增加一个目标数据包的 D3D12 试运行。

产物：

- `environment.json`
- `build.txt`
- `preflight-tests.txt`
- `timing-calibration.csv`

准入条件：

- 工作区干净
- 两个后端构建通过
- CTest 全部通过
- 默认与压力路径没有正确性错误
- 空计时 P99 不超过 0.01 ms
- 三次短基准中，同一 pass、同一策略的运行级中位数极差不超过三次中位数总中位值的 5%；超过时冷却并整轮重跑，不能删除单次结果

本轮不产生论文性能样本。

### 第 1 轮：工作负载发现与目标清单生成

基准：DOD 固定串行增量策略。

规模：

~~~text
18 个场景单元 × 64 帧 = 1152 个正式发现帧
最多形成 1152 × 6 = 6912 条 pass 工作负载记录
~~~

其中 `k=0` 的 18 个状态只用于初始化和记录首帧成本；每个 pass 的目标选择池最多是 `18 × 63 = 1134` 个状态。发现阶段按场景表顺序运行，每个场景新建 pipeline，从 `k=0` 连续推进到 `k=63`，禁止跨场景复用拓扑。

测量：

- 每个 pass 的执行前、规划期间和执行后字段
- 活动三角形、队列规模、预算余量、修改记录和上传数据包规模
- 输入哈希、拓扑哈希、网格哈希和正确性结果
- 特征获取耗时

本轮不比较策略速度，不生成 winner。

产物：

- `workload-discovery.csv`
- `target-states.csv`
- `selection-report.md`

验收：

- 18 个场景和 1152 帧全部存在
- 每个场景的采样编号连续且相机哈希可重放
- 目标清单只依赖允许的选择字段
- 训练与留出分组已经写入清单

轨迹 C 的原始数据可以与其他场景同批采集，但在第 7 轮模型和阈值冻结前保持封存。性能交叉的描述性总表也应在模型冻结后再合并轨迹 C，避免研究人员根据留出结果反复调整特征和门槛。

### 第 2 轮：五个 CPU Pass 的核心配对

基准：

- 评分：串行刷新对并行刷新
- 拓扑：串行立即提交对并行辅助
- 网格提交：串行脏数据、并行脏数据、串行全量三者两两比较

设置：

- 数量门槛全部设为 0
- 串行固定 1 个线程，并行上限固定 8 个线程
- 每个策略预热 5 次，正式记录 30 次
- 两策略按绝对块编号 `b=0..34` 交替 AB、BA；`b=0..4` 预热，`b=5..34` 记录
- 网格提交按下表六种顺序循环；同样先执行 5 个预热块，再记录 30 个块
- 状态复制时间单列，不进入 pass 墙钟时间

两策略中的 A/B 定义为：评分 A=`SerialRefresh`、B=`ParallelRefresh`；拓扑 A=`SerialImmediate`、B=`ParallelAssisted`。网格提交六种顺序固定为：

| `b mod 6` | 第一个 | 第二个 | 第三个 |
| ---: | --- | --- | --- |
| 0 | `SerialDirty` | `ParallelDirty` | `SerialFull` |
| 1 | `SerialDirty` | `SerialFull` | `ParallelDirty` |
| 2 | `ParallelDirty` | `SerialDirty` | `SerialFull` |
| 3 | `ParallelDirty` | `SerialFull` | `SerialDirty` |
| 4 | `SerialFull` | `SerialDirty` | `ParallelDirty` |
| 5 | `SerialFull` | `ParallelDirty` | `SerialDirty` |

因此每个两策略目标写 60 行正式样本和 10 行预热执行计数；每个网格目标写 90 行正式样本和 15 行预热执行计数。30 个正式网格块恰好让六种排列各出现 5 次。

规模：

| Pass | 冻结输入 | 策略数 | 正式执行数量 |
| --- | ---: | ---: | ---: |
| 合并评分 | 144 | 2 | 8640 |
| 细分评分 | 144 | 2 | 8640 |
| 合并拓扑 | 144 | 2 | 8640 |
| 细分拓扑 | 144 | 2 | 8640 |
| 网格提交 | 144 | 3 | 12960 |
| **合计** | **最多 720 个专用输入** |  | **47520** |

CPU pass 预热共 7920 次，不进入正式样本。

整个目标状态的正确性条件：

- 两种结果规范化等价
- 所有正确性计数为 0
- 同一配对块使用相同冻结输入编号

速度配对还要求并行行的实际方式确实为并行、实际线程数量至少为 2。安全分块不足而回退串行不使整轮失败；该记录保留为可并行性结果，但不进入串并行速度差。任何结果不等价、预算越界、队列错误、非法邻接或裂缝都使对应目标失败，修复后必须重跑整个目标，不能只补失败策略。

### 第 3 轮：CPU 上传核心配对和 D3D12 交叉检查

OpenGL 主实验：

~~~text
18 个场景单元 × 每场景 8 个数据包 × 2 种策略 × 30 次
= 8640 次正式上传
~~~

预热 1440 次。每个数据包必须满足：

- 不是首次网格建立
- 不发生缓冲区扩容
- 请求 `DirtyRange` 时没有帧槽积压回退
- 两种策略的数据包编号和完整缓冲区大小相同

上传也按 35 个块执行：偶数块先 `DirtyRange` 后 `FullBuffer`，奇数块反序；前 5 个块预热，后 30 个块记录。`wallMs` 只包围上传调用。每次调用使用已经在计时外恢复为相同旧内容的独立实验缓冲区，不能直接继承上一个策略写完的目标内容。30 个正式块结束后，每种策略各执行一次不计时上传，再读取完整缓冲区并比较顶点、索引、容量和绘制参数哈希；读取与验证不进入 `wallMs`，也不插入正式计时块之间。

D3D12 交叉检查：

~~~text
2 个地形 × 3 条轨迹 × 4 个中预算数据包 × 2 种策略 × 30 次
= 1440 次正式上传
~~~

D3D12 预热 240 次。两个后端分别统计，不合并为共同中位数。

第 2、3 轮主矩阵合计 56160 次正式策略执行，与既定核心规模一致。

### 第 4 轮：统计判定和性能交叉估计

本轮首先分析，不自动扩大实验。

本轮只分析 `analysisSplit=train` 的轨迹 A/B。轨迹 C 的原始配对结果已经采集，但在第 7 轮模型、阈值和报告模板冻结前不生成胜负标签、不画性能交叉图，也不参与继续条件判断。

每个冻结输入计算：

- 各策略 p50、p95 和最大值
- 配对耗时差
- 10000 次 bootstrap 的 95% 置信区间
- 请求/实际方式和回退分布
- 完整 pass 成本及内部成本构成

平局规则：

- 配对差的 95% 置信区间包含 0；或
- 中位差小于 `max(0.01 ms, 较快策略中位时间的 3%)`

稳定性能交叉要求：

1. 至少存在两个方向相反的获胜区域
2. 每个区域至少有 8 个独立冻结输入
3. 每个区域至少 70% 的输入支持同一策略
4. 区域聚合置信区间不包含 0
5. 相反结果至少在两个场景单元中复现

若满足区域条件但交叉附近样本稀疏，可以为该 pass 增加一次边界确认：

- 每个不确定 pass 最多增加 24 个新冻结输入
- 只按主要特征与估计区间的距离选择，不按单次获胜结果选择
- 保持 5 次预热和 30 次正式重复
- 若确认后仍不能形成稳定单一边界，正式结论为“没有稳定的单阈值”

### 第 5 轮：固定策略端到端基准

配置：

1. Classic 外部参考
2. DOD 固定串行 + 增量输出
3. DOD 最大安全并行 + 增量输出
4. DOD 固定串行 + 全量输出
5. DOD 最大安全并行 + 全量输出

其中全量输出仍使用 `SerialFull`；第一轮不声称存在并行全量网格提交。

五种配置的精确定义为：

| 编号 | 评分 | 拓扑 | 网格提交 | CPU 上传 | 数量下限 |
| --- | --- | --- | --- | --- | --- |
| C0 Classic | Classic 固有串行 | Classic 固有串行 | Classic 增量网格 | `DirtyRange` | Classic 当前默认 |
| C1 DOD 串行增量 | 两个评分均串行 | 两个拓扑均串行 | `SerialDirty` | `DirtyRange` | 不适用 |
| C2 DOD 最大安全并行增量 | 两个评分均请求并行 | 两个拓扑均请求并行辅助 | `ParallelDirty` | `DirtyRange` | 评分/网格 256，细分拓扑 32，合并拓扑 160 |
| C3 DOD 串行全量 | 两个评分均串行 | 两个拓扑均串行 | `SerialFull` | `FullBuffer` | 不适用 |
| C4 DOD 最大安全并行全量 | 两个评分均请求并行 | 两个拓扑均请求并行辅助 | `SerialFull` | `FullBuffer` | 评分 256，细分拓扑 32，合并拓扑 160 |

第 2 轮把数量下限设为 0 是为了测出性能交叉；第 5 轮固定基准恢复当前生产保护值，不能把实验专用的 0 当成最终策略。

每种配置的每一次轨迹重复：

- 使用轨迹前 8 个点独立预热
- 预热后重置拓扑
- 执行一次完整 64 点正式轨迹
- 五种配置按下面的固定循环顺序运行

令轨迹重复编号为 `r=0..4`。第 `r` 次重复的配置顺序为 `C_r,C_(r+1),...,C_(r+4)`，下标对 5 取模。这样每种配置在同一场景中恰好各占一次第一到第五执行位置。18 个场景按 UTF-8 字符串 `20260830|scenarioId` 的 FNV-1a 64 位散列升序固定；散列相同时按场景编号升序，不能按某次耗时重新排列。

规模：

~~~text
18 × 5 种配置 × 5 次重复 × 64 帧 = 28800 条正式帧记录
预热另有 3600 帧
每次正式轨迹之后另运行 64 帧不计时正确性重放，共 28800 帧
~~~

测量：

- 整帧、CPU 更新和各 pass 时间
- p50、p95、最大值和 16.6 ms 超限率
- 正确性、回退、上传字节和阶段实际方式

本轮用于确认独立 pass 现象能否在完整流水线中观察到，不能替代第 2、3 轮的冻结配对。

端到端计时关闭全局拓扑诊断和额外证据扫描；每条计时轨迹结束后立即以相同场景、配置和相机清单从根拓扑重放 64 点，打开全部正确性检查但丢弃时间。计时运行自身仍执行公共资源结果检查。计时与正确性重放的逐帧规范化结果哈希必须相同，否则该“场景 × 配置 × 重复”整体无效。

轨迹 C 的端到端数据与 CPU pass 数据采用同一封存规则：可以完成采集和正确性校验，但第一研究门槛只读取轨迹 A/B。模型和阈值冻结后才打开 C 的性能结果。

### 第一研究门槛

完成第 4、5 轮后：

- 至少三个具体 pass 出现稳定且位置不同的性能交叉，继续通用 pass 级自适应路线
- 某个策略始终被支配，从后续策略集合移除
- 细分拓扑没有真实并行区域时，保留串行实现和负面结果
- 不足三个 pass 成立时，收缩为单阶段或更窄的自适应批处理问题

没有通过该门槛时，不实施复杂模型和整帧离线参考。

### 第 6 轮：离线参考与阶段耦合

本轮仅在第一研究门槛通过后执行。

`Greedy pass oracle` 从同一冻结输入的合法策略中选择各 pass 最快结果。`Frame-level oracle` 从同一帧初始状态执行合法策略组合，测量阶段组合是否存在耦合。

模型冻结前只从轨迹 A/B 选择 24 个整帧状态：

- 12 个训练场景单元各 1 个
- 另外 12 个覆盖性能交叉附近、预算压力和高依赖状态

第 2 轮不同 pass 可以选择不同帧，因此不能把互不对应的 144 个目标状态直接拼成整帧贪心结果。对这 24 个整帧状态，需要沿贪心执行路径重新冻结各 pass 输入并运行合法策略，再与从同一帧初始状态开始的组合重放比较。整帧离线参考固定使用 OpenGL 主后端。

第 7 轮模型冻结后，再从轨迹 C 的 6 个留出场景各选 1 个整帧状态计算留出离线参考；这 6 个状态不能反过来修改特征、门槛或模型结构。

组合上限为 `2 × 2 × 2 × 2 × 3 × 2 = 96`。先删除已经被支配、不适用或确定回退的组合。

为控制开销：

1. 所有剩余组合先预热 3 次并正式记录 5 次
2. 每个状态保留前三个组合
3. 前三名重新预热 5 次并正式记录 30 次
4. 最终离线参考只使用第二阶段的独立重复

主要输出：

~~~text
oracle_gap = (T_greedy_oracle - T_frame_oracle) / T_frame_oracle
~~~

差距小支持阶段近似解耦；差距大则说明在线方法需要协调多个 pass，不能只做局部选择。

### 第 7 轮：特征模型

候选方法按复杂度递增：

1. 单一工作量阈值
2. 二维查找表
3. 分段线性模型或浅层决策树

只允许使用执行前和规划期间特征。执行后字段只能解释错误，不能进入同一状态的决策。

数据划分：

- 轨迹 A、B 的 12 个场景单元用于训练和标定
- 轨迹 C 的 6 个场景单元保持封存，用作最终混合工作负载测试
- 相邻帧不进行随机拆分

模型主要评价相对离线参考的额外耗时和时间预算超限率，不以分类准确率为主要目标。简单阈值已经接近离线参考时，不继续增加模型复杂度。

### 第 8 轮：在线策略和泛化验证

新增两种端到端配置：

- 训练场景上选出的 `Best static configuration`
- `Workload-aware adaptive DOD`

为避免把不同时间段的数据直接拼接，第 8 轮重新交错运行五种可执行配置：Classic、DOD 固定串行增量、DOD 最大安全并行增量、最佳静态组合和在线策略。第 5 轮的两种全量输出对照不自动重跑，除非第 4 轮证明它们仍属于最终有效策略集合。

正式规模为：

~~~text
18 × 5 种配置 × 5 次重复 × 64 帧 = 28800 条正式帧记录
预热另有 3600 帧
~~~

最终报告至少包含：

- CPU 更新 p50、p95 和 16.6 ms 超限率
- 相对 `Best static configuration` 的收益
- 24 个离线参考状态上的中位和 P95 额外耗时
- 特征采集与决策开销，占 CPU 更新时间的比例
- 策略切换次数、回退率和回退原因
- 轨迹 C 留出结果
- 所有正确性计数

继续目标固定为：中位额外耗时不超过 5%，P95 额外耗时不超过 10%，特征采集与决策开销不超过 CPU 更新时间的 5%。第 0 轮只判断当前计时噪声是否允许执行实验，不再改动这三个目标。

其中轨迹 C 的最终留出子集包含 `6 × 5 × 5 × 64 = 9600` 条帧记录，在模型和所有门槛冻结前不得用于调参。

## 8. 性能交叉与阈值估计

### 8.1 评分 pass

评分首先检验单一队列规模阈值：

~~~text
N < T  -> 串行
N >= T -> 并行
~~~

在训练场景中选择使总预测 pass 时间或相对离线参考额外耗时最小的 `T`，而不是选择分类准确率最高的 `T`。以冻结输入为单位 bootstrap，报告 `T` 的 95% 置信区间，并在完整留出轨迹上验证。

合并评分和细分评分独立拟合，禁止共享一个最终阈值。

### 8.2 拓扑 pass

拓扑不预设只有一个候选数量阈值。正式比较两级策略：

~~~text
执行前门槛：
  队列规模或预算条件不足 -> 直接串行

规划期间门槛：
  建立候选快照和安全分块
  非空分块与安全内部候选足够 -> 并行辅助
  否则 -> 串行收敛
~~~

第二级判断已经支付候选快照和分块成本。若在线模型在此处选择串行，该规划成本仍必须进入在线总成本。

`earlyCommitCount`、实际串行尾部时间和实际线程利用率属于执行后字段，只能解释结果。

### 8.3 网格提交

网格提交有三个策略，可能形成两个边界：

~~~text
少量修改：SerialDirty
中等修改：ParallelDirty
接近全量或高度破碎：SerialFull
~~~

正式判断同时使用脏三角形数量、脏比例和预计区间碎片度。三组两两比较的置信区间使用 Holm 校正。若其中一个策略始终被支配，删除该策略后再拟合剩余边界。

### 8.4 CPU 上传

上传优先检验二维边界：

- 脏字节比例
- 更新区间数量

缓冲区扩容、首次建立和 D3D12 帧槽积压是回退标签，不进入 `DirtyRange` 与 `FullBuffer` 的主动策略胜负比较。

## 9. 特征时序

| Pass | 执行前可用 | 规划期间可用 | 仅执行后分析 |
| --- | --- | --- | --- |
| 评分 | 队列规模、线程上限、上一帧评分/建堆成本 | 无必要的额外完整扫描 | 实际线程数量、评分时间、建堆时间 |
| 拓扑 | 队列规模、预算余量、请求修改规模 | 候选数量、内部/边界比例、非空分块、预计不均衡 | 提前提交数量、真实闭合深度、串行尾部时间 |
| 网格提交 | 活动三角形、修改记录数量 | 预计脏三角形、预计区间数量和碎片度 | 实际写入数量、实际区间和墙钟时间 |
| CPU 上传 | 数据包大小、更新范围、缓冲区容量和帧槽状态 | 不需要运行上传才能得到的派生比例 | 实际上传字节、图形接口耗时和回退 |

CSV 字段必须使用 `pre_`、`planning_`、`post_` 前缀区分这三类数据。无法证明在决策前可得的字段不得进入在线模型。

## 10. 数据输出与恢复

输出目录固定为：

~~~text
benchmark-output/formal/<runId>/
├── metadata/
│   ├── environment.json
│   ├── build.txt
│   └── run-manifest.csv
├── manifests/
│   ├── scenarios.csv
│   ├── camera-samples.csv
│   └── target-states.csv
├── raw/
│   ├── discovery/
│   ├── cpu-pass/
│   ├── upload-opengl/
│   ├── upload-d3d12/
│   ├── fixed-baseline/
│   ├── frame-oracle/
│   └── adaptive/
├── analysis/
└── logs/
~~~

每个原始 CSV 至少绑定：

- `runId`、`schemaVersion`、Git commit 和配置哈希
- 场景、pass、冻结输入、重复和执行顺序
- 请求/实际方式、线程数量和回退原因
- 允许的工作负载字段和特征时序
- 完整 pass 时间和内部解释时间
- 输入、结果和配对哈希
- 正确性布尔值与失败掩码

`runId` 固定采用 `YYYYMMDD-HHmmss-<commit前8位>-rNN`，例如 `20260831-213000-a1b2c3d4-r01`。同一批正式实验共用一个 `runId`；试运行使用 `-pilotNN` 后缀，不能写入正式目录。

`configHash` 使用 SHA-256，输入按以下顺序连接：可执行文件哈希、完整 Git commit、规范化场景清单内容、相机清单文件哈希、目标清单文件哈希、策略与线程参数、预热和重复次数、构建配置、图形后端与资产哈希。字段以 UTF-8、LF 换行和 CSV 规范化顺序编码。恢复运行时重新计算；任一输入变化都拒绝复用已有完成文件。

每个场景单元写入独立临时文件，全部校验通过后原子改名为完成文件。`--experiment-resume` 只能跳过配置哈希、预期行数和正确性检查同时通过的文件。

原始数据只追加或新建，不在分析脚本中改写。清洗、平局标签、异常值标记和 bootstrap 结果写入 `analysis/`。

## 11. 基准与公平性

### 11.1 单 Pass 基准

- DOD 固定串行是所有 CPU pass 的基础参考
- 最大安全并行表示请求并行，但不绕过安全依赖
- Classic 只作为端到端外部参考，不作为 DOD 内部 pass 的配对样本
- 状态复制成本不进入策略时间，但必须报告实验总成本
- 并行策略的线程调度、同步、候选快照、分块和结果整理必须计入完整 pass 时间

### 11.2 端到端基准

固定比较：

- Classic
- DOD 固定串行增量
- DOD 最大安全并行增量
- DOD 固定串行全量
- DOD 最大安全并行全量
- 条件成立后增加最佳静态组合和在线策略

不同配置独立预热并重置拓扑。运行顺序按重复编号轮换，不能总让某一配置最后执行。

### 11.3 禁止事项

- 不用整帧重复近似同一 pass 的冻结输入配对
- 不把回退后的串行记录标记为并行性能
- 不把现有 256、32、160 写成测量结论
- 不按实测胜负选择核心目标状态
- 不随机拆分同一轨迹的相邻帧
- 不只报告线程内部提交时间而遗漏规划和串行尾部
- 不因论文叙事需要而删除“没有性能交叉”的 pass

## 12. 实施计划

### 实施 1：解除实验数据截断

修改：

- `TerrainLodPassTrace.h`：增加评分和网格提交的最小并行工作量字段
- `DataOrientedRoamTypes.h`：映射公共策略字段
- `DataOrientedRoamQueues.cpp`：用策略字段替代固定 `256`
- `DataOrientedRoamMeshEmit.cpp`：用策略字段替代固定 `256`
- `DataOrientedRoamPassExperiment.cpp`：正式模式把五个数量门槛设为 0
- `TerrainLodBenchmark.cpp`：增加计时标定和正式参数解析

验收：

- 生产默认行为和现有回归不变
- 冻结实验可以在小于 256 的有效工作量上真实启动多个线程
- 回退原因仍准确
- 新增参数均有命令行解析和错误输入测试

### 实施 2：正式场景和轨迹生成

新增统一场景结构和 A/B/C 轨迹生成器，由无窗口和应用级入口共同使用。禁止在两个入口分别维护坐标公式。

验收：

- 18 个场景均可单独运行
- 同一场景在两个入口生成相同的相机和设置哈希
- 三角形预算不会被内置压力路径覆盖

### 实施 3：工作负载发现和目标选择

新增发现配置、特征 CSV、确定性选择器和目标清单。

验收：

- 1152 个状态完整
- 不读取任何策略耗时即可重建相同目标清单
- 不足 8 个唯一状态时明确报告覆盖不足

### 实施 4：正式 CPU 配对

让现有冻结执行器读取场景和目标清单，支持 pass 筛选、运行编号、恢复执行和完整元数据。拓扑串行分支改为直接运行生产 `SerialImmediate` 收敛；候选快照只用于输入编号和向串行行回填共同分类，不允许串行计时执行 `BuildInterior*Chunks` 或 `CommitInterior*ChunksSerial`。所有计时副本在进入阶段前关闭 `EnablePassEvidence` 和 `EnableTopologyValidation`，停表后由实验框架统一计算哈希和运行检查，避免默认场景与压力场景携带不同诊断成本。

验收：

- 单场景、单 pass 可以独立重跑
- 同一目标的所有策略使用相同输入编号
- 预期行数、AB/BA 或六排列顺序正确
- 状态复制和策略时间分列
- 拓扑串行时间不包含候选快照、分块或串行分块预提交
- 拓扑并行辅助时间包含候选快照、分块、提交、整理和串行尾部
- 两条真实生产路径的规范化结果和正确性计数一致
- 同一冻结输入在诊断开关开启和关闭时产生相同规范化结果
- 默认与压力场景的计时副本使用相同诊断开关

### 实施 5：正式上传配对

把“选择最先出现的稳定数据包”改为读取目标清单，补齐显式预算、轨迹、算法和输出目录参数。为上传实验建立独立缓冲区，保存目标状态上一帧的完整缓冲区镜像；每个策略计时前在计时外恢复相同旧内容，正式块之间不执行读取等待。

验收：

- 两个后端可以重建同一目标数据包
- 初始化、扩容和积压回退不进入有效配对
- D3D12 重放后帧槽状态恢复
- 两种策略的上传前缓冲区内容和容量哈希相同
- 30 个计时块完成后执行不计时读取，最终顶点、索引和绘制参数哈希一致

### 实施 6：统计与报告脚本

新增 `scripts/analyze_pass_crossover.py`，优先使用 Python 标准库；确需第三方包时固定版本并写入依赖说明。

脚本至少完成：

- 架构版本与配置哈希检查
- 有效样本和回退样本分离
- p50、p95、最大值
- 配对 bootstrap 和 Holm 校正
- 平局、获胜区域和性能交叉判定
- 单阈值、二维表和留出额外耗时
- 每个 pass 独立 CSV 与 Markdown 摘要

分析脚本使用固定随机种子，并包含小型合成数据单元测试。

### 实施 7：批量调度和中断恢复

实现正式 PowerShell 调度脚本、逐场景日志、失败清单和恢复执行。

验收：

- 中途终止后不会覆盖已完成数据
- 改变 commit 或配置后拒绝混用旧输出
- 全部场景结束后检查预期行数和正确性

### 实施 8：正式采样前最终试运行

先使用：

~~~text
2 个地形 × 1 条轨迹 × 1 个中预算
每个策略预热 1 次、正式重复 3 次
~~~

验证完整发现、选择、CPU 配对、上传、分析和恢复流程。试运行数据单独存放，确认无误后删除或标记为 `pilot`，不并入正式数据。

### 条件实施 9：整帧离线参考

仅在第一研究门槛通过后，实现从同一帧初始状态重放策略组合的入口。它必须支持策略组合清单、确定性剪枝、两阶段重复、整帧结果哈希和独立输出，不能用各 pass 时间简单相加冒充整帧结果。

### 条件实施 10：在线选择与开销记录

模型冻结后再接入生产流水线。新增特征采集时间、决策时间、请求方式、实际方式、回退、切换和最小驻留统计。在线入口默认关闭；关闭时不得执行额外扫描或复制实验状态。

## 13. 正式开跑检查表

只有全部满足后才开始第 1 轮：

- [ ] 评分和网格提交的实验并行下限可以设为 0
- [ ] 18 个场景清单通过解析和哈希测试
- [ ] A/B/C 轨迹由公共生成器产生
- [ ] 目标选择器不读取策略耗时
- [ ] CPU 配对入口可以按目标清单和 pass 单独运行
- [ ] 上传入口可以精确重建指定数据包
- [ ] 三角形预算和场景参数不会被应用内置路径覆盖
- [ ] 运行元数据包含 commit、硬件、驱动、构建和配置哈希
- [ ] 分析脚本通过合成数据测试
- [ ] OpenGL、D3D12 构建和全部 CTest 通过
- [ ] 默认路径与压力路径验收通过
- [ ] 小规模端到端试运行通过
- [ ] 正式输出目录为空，或者使用新的 `runId`

## 14. 预计执行顺序

~~~text
入口补齐
  -> 小规模端到端试运行
  -> 第 0 轮环境冻结
  -> 第 1 轮工作负载发现
  -> 冻结目标状态清单
  -> 第 2 轮 CPU pass 配对
  -> 第 3 轮上传配对
  -> 第 4 轮统计与性能交叉判定
  -> 第 5 轮固定策略端到端确认
  -> 第一研究门槛
      -> 不通过：收缩或停止通用自适应路线
      -> 通过：第 6 轮离线参考
               -> 第 7 轮特征模型
               -> 第 8 轮在线策略和泛化验证
~~~

这个顺序保证先回答“性能交叉是否真实存在”，再回答“能否预测”，最后回答“在线选择是否带来端到端收益”。任何后续模型都不能反过来改变第 1 轮目标清单或第 2、3 轮原始数据。
