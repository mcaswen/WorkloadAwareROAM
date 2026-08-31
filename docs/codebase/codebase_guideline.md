# 模块架构事实文档输出规范

本规范用于生成面向 AI Agent 的模块级架构事实文档。不是概览、README、设计提案或面向人类阅读的架构总结。

核心目标是：

**尽可能完整、准确、可追溯地记录当前代码的真实结构、职责、数据流、控制流、状态变化、依赖关系和重要实现细节，使后续 Agent 能在减少重复扫描源码的情况下建立接近真实代码的系统认知。**

优先级：

**真实性 > 完整性 > 可追溯性 > 细节 > 简洁性。**

允许文档很长。不要为了降低 Token 数量省略对理解模块有意义的信息。

# 1. 基本原则

## 1.1 只描述当前真实代码

所有结论必须以当前代码为依据。

禁止：

* 根据文件名猜测职责。
* 根据类名推断未验证行为。
* 根据历史规划描述尚未实现的设计。
* 根据“正常架构应该如此”补全不存在的逻辑。
* 把 TODO、注释中的设想或历史文档当成当前实现。
* 为了让架构显得合理而忽略实际存在的职责混杂、重复逻辑或错误依赖。

如果真实代码存在不合理架构，也必须如实记录。

例如：

> `FlowFieldBuildSystem` 当前同时负责请求调度、缓存维护和部分 Build 状态管理。

不要自行改写为：

> 调度层负责请求编排，缓存层负责缓存。

除非代码实际如此。

## 1.2 事实、推断和规划必须严格区分

文档中的内容必须区分：

* **FACT**：可以直接从当前代码确认。
* **INFERENCE**：根据多处代码关系推导出的高可信结论，但代码没有直接声明。
* **UNCERTAIN**：无法仅根据当前扫描确认。
* **PLANNED**：来自规划文档、TODO 或注释，但当前代码尚未实现。
* **DEPRECATED / LEGACY**：仍存在于代码中，但已确认属于旧路径或废弃实现。

不得把推断写成事实。

如果无法确认，明确写：

`UNCERTAIN: 当前扫描无法确认该对象最终由谁释放。`

不要自行填补答案。

## 1.3 所有重要结论必须可定位

对于关键事实，尽可能附带：

* 文件路径
* 类型名
* 方法名
* 字段名
* 接口名
* 调用入口
* 被调用位置
* 必要时记录相关代码区域

例如：

`Assets/Scripts/Navigation/FlowField/Build/FieldBuilder.cs`
`FieldBuilder.BuildField()`

文档目标是让后续 Agent 能直接定位源码，而不是重新全文搜索。

# 2. 扫描要求

生成文档前必须扫描：

* 当前模块全部源码文件。
* 与该模块直接依赖的核心文件。
* 调用该模块的主要上层入口。
* 与模块共享关键数据结构的代码。
* 相关接口和抽象基类。
* 相关配置。
* 生命周期入口。
* 初始化和销毁逻辑。
* Job / Thread / Task / Coroutine / GPU 调度路径。
* 与该模块相关的测试。
* 与当前实现直接相关的开发规范和规划文档。

不要仅扫描目标文件夹后就结束。

如果模块边界不明确，应沿调用关系和核心数据读写关系继续扫描，直到能够解释主要运行路径。

# 3. 模块基本信息

记录：

* 模块名称。
* 物理目录。
* 主要 namespace。
* 模块主要职责。
* 当前实际承担的附加职责。
* 明确不负责的内容。
* 主要上层调用者。
* 主要下层依赖。
* 主要输入。
* 主要输出。
* 核心运行环境。
* 生命周期。
* 是否为性能敏感模块。
* 是否存在多线程、Job、异步或 GPU 行为。

如果“设计职责”和“实际职责”不同，应同时记录。

# 4. 完整目录和文件清单

列出模块内所有有意义的源码文件。

对每个文件记录：

* 完整路径。
* 文件内包含的类型。
* 文件真实职责。
* 主要调用者。
* 主要依赖。
* 是否保存核心状态。
* 是否包含算法。
* 是否包含调度逻辑。
* 是否包含公共接口。
* 是否存在明显职责混杂。
* 是否属于 Legacy / Debug / Experimental。

不要只列所谓“核心文件”。

简单文件可以简短说明，但不得因为文件简单而完全忽略。

# 5. 类型清单

记录模块中的重要：

* class
* struct
* interface
* enum
* record
* ECS Component
* System
* Job
* ScriptableObject
* MonoBehaviour
* Native container wrapper
* 配置类型
* 数据传输类型

每个类型至少记录：

## Identity

* 类型名。
* 文件路径。
* 继承关系。
* 实现接口。
* 泛型约束。
* 可见性。

## Responsibility

* 当前真实职责。
* 不负责什么。
* 是否承担多个职责。
* 它存在于整个模块的哪个层级。

## Ownership

* 谁创建。
* 谁持有。
* 谁修改。
* 谁销毁。
* 生命周期长度。
* 是否跨帧。
* 是否共享。
* 是否线程安全。

不能确认时明确标记。

# 6. 字段与重要状态

对核心类型记录所有影响行为的重要字段。

至少包括：

* 字段名。
* 类型。
* 初始值或初始化位置。
* 谁写入。
* 谁读取。
* 代表什么状态。
* 生命周期。
* 是否允许为空。
* 是否只读。
* 是否缓存。
* 是否派生数据。
* 是否存在失效条件。
* 是否跨线程访问。

尤其记录：

* queue
* cache
* active set
* dirty set
* version
* flag
* index
* generation
* handle
* native container
* job handle
* state enum
* current / pending / completed 状态
* ownership 引用

不要机械记录普通常量或无意义字段。

# 7. 方法与关键行为

方法记录应以“帮助 Agent 理解真实执行逻辑”为目标，不要求对所有方法进行同等粒度描述。

## 7.1 方法分级

### A. 核心方法

以下方法需要详细记录：

* 模块主要入口。
* 核心算法入口及关键阶段。
* 调度、状态转换和生命周期方法。
* 修改核心共享状态的方法。
* 缓存创建、命中、失效和提交路径。
* Job / Task / GPU 调度与同步方法。
* 包含复杂分支或重要副作用的方法。
* 修改后可能影响多个系统的方法。

每个核心方法记录：

* 方法名和签名。
* 核心职责。
* 主要调用者或调用时机。
* 主要状态读写。
* 关键下游调用。
* 重要副作用。
* 必要的前置条件、分支或失败路径。

复杂方法应进一步在“控制流”章节展开，不需要在这里重复完整流程。

### B. 普通逻辑方法

对于承担明确局部逻辑，但不决定模块整体行为的方法，只记录：

* 方法名。
* 一句话职责。
* 必要时记录关键状态修改或重要调用关系。

例如：

### C. 简单方法

以下方法原则上无需单独解释：

* getter / setter
* 简单构造函数
* 单纯字段赋值
* 无逻辑包装器
* 明显的类型转换
* 简单转发方法
* trivial helper

如果需要保证符号完整性，可以只在文档末尾的方法索引中列出名称。

## 7.2 方法选择原则

主要详细记录那些能够回答以下问题的方法：

* 系统从哪里进入？
* 核心状态在哪里改变？
* 数据在哪里产生、转换和提交？
* 调度在哪里发生？
* 生命周期在哪里推进？
* 缓存和资源在哪里失效？
* 哪些方法构成关键执行路径？
* 哪些方法修改时最容易破坏模块行为？

如果一个方法不会影响 Agent 对模块架构、数据流、状态或修改风险的理解，可以降低记录粒度。

## 7.3 避免重复

方法章节负责描述“这个方法承担什么关键行为”。

详细执行顺序应放在：

* 控制流
* 数据流
* 核心算法
* 生命周期
* 并发模型

等对应章节中。

不要在多个章节重复完整描述同一个方法。

目标是保留足够的事实信息，而不是把源码重新转写成自然语言。


# 8. 方法级控制流

对关键方法展开真实控制流。

不要只写：

`Update() → Schedule() → Build()`

应尽可能记录真实分支。

例如：

```text
OnUpdate()
  ↓
检查 pending request
  ├─ 无请求 → return
  ↓
检查现有 active build
  ├─ 存在 → 更新状态 / return
  ↓
TryGetCachedField()
  ├─ hit → CommitCachedResult()
  └─ miss
       ↓
     BuildRequest()
       ↓
     ScheduleJob()
```

需要记录：

* early return
* branch
* fallback
* retry
* error path
* cache hit/miss
* sync/async 分支
* feature flag
* debug path
* conditional compilation

# 9. 数据模型

详细记录模块使用的核心数据模型。

包括：

* 数据结构。
* 数据之间的引用关系。
* source of truth。
* 派生数据。
* 临时数据。
* 缓存。
* 中间表示。
* 输入表示。
* 输出表示。

尤其明确：

**哪个数据是真实源数据，哪些只是副本或派生结果。**

例如：

```text
NavigationGrid
    ↓ source
SectorData
    ↓ derived
IntegrationField
    ↓ derived
VectorField
    ↓ consumed by
MovementSystem
```

记录每次转换由哪个方法完成。

# 10. 数据流

至少记录所有核心数据流。

每条数据流必须回答：

1. 数据在哪里创建？
2. 数据最初是什么格式？
3. 谁拥有它？
4. 谁第一次读取？
5. 谁修改？
6. 经过哪些转换？
7. 中间结果保存在哪里？
8. 最终谁消费？
9. 什么时候失效？
10. 谁负责重建或删除？

尽量使用：

`类型.字段 → 方法 → 类型.字段`

而不是抽象名称。

例如：

```text
BuildRequest.target
→ FieldBuildScheduler.Enqueue()
→ _pendingRequests
→ FieldBuildScheduler.TrySchedule()
→ FieldBuilder.Build()
→ FieldBuildResult
→ FieldStore.Commit()
→ RuntimeFieldData
```

---

# 11. 状态变化

如果模块包含状态，应单独记录状态转换。

例如：

```text
Pending
  ↓ Schedule
Building
  ↓ Complete
Ready
  ↓ Dirty
Invalid
  ↓ Rebuild
Building
```

对于每个转换说明：

* 哪个方法触发。
* 条件是什么。
* 修改哪些字段。
* 是否产生副作用。
* 是否可能回退。
* 是否跨帧。

如果没有正式 State Machine，但代码实际上通过多个 bool / enum / collection 表达状态，也必须恢复出真实状态关系。

---

# 12. 调用关系

记录核心调用图。

至少区分：

## 上层入口

谁调用该模块。

## 模块内部

核心类之间如何调用。

## 下游依赖

该模块调用哪些外部系统。

如果存在反向调用、callback、event、delegate、observer、消息系统等，也必须记录。

不要只记录静态依赖；动态回调关系同样重要。

---

# 13. 生命周期

对于模块整体和重要对象记录：

```text
Creation
→ Initialization
→ Runtime Update
→ Reset / Rebuild
→ Shutdown
→ Dispose
```

必须确认：

* 初始化入口。
* 初始化顺序。
* 是否依赖其他模块先初始化。
* Update 顺序。
* 数据何时创建。
* 数据何时清空。
* Scene reload 是否重建。
* Domain reload 行为。
* Dispose 是否完整。
* Native resource 是否显式释放。

生命周期不明确的部分标记 `UNCERTAIN`。

---

# 14. 线程、Job 和异步模型

如果存在并发行为，应详细记录：

* 主线程负责什么。
* Worker 负责什么。
* 哪些方法在 Job 中执行。
* Job 类型。
* Job dependency。
* JobHandle 保存在哪里。
* Schedule 时机。
* Complete 时机。
* 是否存在同步点。
* 哪些数据可以并行读。
* 哪些数据并行写。
* 是否存在 race 风险。
* 哪些容器使用 NativeArray / NativeList / NativeQueue 等。
* 是否 Burst 编译。
* 哪些路径因为 API 限制必须回主线程。

对于 Task / async / coroutine 同样处理。

---

# 15. 缓存与失效机制

如果存在缓存，应记录：

* Cache key。
* Cache value。
* 存储位置。
* 创建时机。
* 查询时机。
* 命中行为。
* miss 行为。
* 淘汰策略。
* 容量。
* 生命周期。
* invalidation 条件。
* 谁负责失效。
* stale 数据如何避免。
* 是否存在版本号。

缓存失效通常是 Agent 修改时最容易破坏的部分，不得省略。

---

# 16. 核心算法

对核心算法记录实际实现，而不仅写算法名字。

至少说明：

* 输入。
* 输出。
* 核心数据结构。
* 执行阶段。
* 每个阶段对应的方法。
* 关键循环。
* 关键条件。
* 时间复杂度特征。
* 空间复杂度特征。
* 增量还是全量。
* 是否有 early-out。
* 是否有特殊优化。
* 哪些实现细节影响正确性。
* 哪些实现细节影响性能。

如果代码与标准算法不同，应记录实际差异。

不要根据算法名默认它符合教科书实现。

---

# 17. 不变量与隐含约束

主动识别代码中的 invariants。

例如：

* `activeCount <= capacity`
* 某 index 必须始终对应当前 generation。
* 某个 Queue 只能由主线程写入。
* BuildResult Commit 前对应 request 必须仍有效。
* 某 NativeArray 生命周期必须覆盖 Job。
* 某缓存内容必须与 version 一致。

对于没有显式 assert，但从代码中可以确认的约束，标记为：

`INFERENCE: ...`

不变量是后续 Agent 修改代码时的重要保护信息，应尽量完整记录。

---

# 18. 依赖规则

记录真实依赖，而不是理想依赖。

包括：

* 当前哪些模块依赖哪些模块。
* 是否存在反向依赖。
* 是否存在循环或近似循环依赖。
* 是否通过 interface 解耦。
* 是否通过 event / callback 解耦。
* 是否存在 Service Locator / Singleton / static dependency。

如果开发规范规定了理想依赖方向，同时记录：

```text
Expected:
A → B → C

Actual:
A → B
B → C
C → A

STATUS: VIOLATION
```

---

# 19. 公共接口与外部契约

记录模块向外暴露的接口：

* public method
* public property
* event
* interface
* component
* message
* callback
* service
* command
* query API

对每个重要接口说明：

* 谁使用。
* 前置条件。
* 后置条件。
* 副作用。
* 错误行为。
* 是否允许重复调用。
* 生命周期要求。
* 是否线程安全。

---

# 20. 配置与 Feature Flag

记录：

* 配置来源。
* 默认值。
* Inspector / config file / constant。
* runtime override。
* feature flag。
* debug flag。
* conditional compilation。
* platform-specific branch。

说明每个重要配置会改变哪些执行路径。

---

# 21. 错误处理与异常路径

记录真实存在的：

* validation
* exception
* error code
* assert
* Debug.LogError
* fallback
* retry
* silent failure
* early return

对于没有处理的危险情况，也应记录：

`FACT: 当前该路径没有显式错误处理。`

不要替代码假设错误会被其他系统处理。

---

# 22. 性能相关事实

对于性能敏感模块记录：

* hot path。
* 每帧调用的方法。
* 大循环。
* O(N)、O(N²) 等明显复杂度。
* GC allocation。
* Native allocation。
* resize。
* memcpy。
* sort。
* hash lookup。
  -同步点。
* Job Complete。
* CPU/GPU transfer。
* 频繁创建和销毁对象的位置。

如果已有 benchmark，可以引用。

没有 benchmark 时，不得编造性能结论。

可以写：

`INFERENCE: 该循环复杂度为 O(activeCount)。`

不能写：

`该实现性能优秀。`

---

# 23. 测试与验证

记录模块相关测试：

* 测试文件。
* 测试类型。
* 覆盖什么行为。
* 未覆盖什么。
* benchmark。
* validation system。
* debug validation。
* runtime assertion。

如果某个重要路径没有测试，应明确记录。

---

# 24. 已知架构问题

如实记录当前代码中的：

* God File。
* God Object。
* 职责泄漏。
* 重复逻辑。
* 隐式耦合。
* 循环依赖。
* shared mutable state。
* 不明确 ownership。
* 生命周期风险。
* 缓存失效风险。
* 过时实现。
* 临时兼容层。
* TODO。
* 规划与实现不一致。

这部分是事实记录，不要求立即提出重构方案。

不要为了“架构文档看起来漂亮”隐藏技术债。

# 25. 规划与真实实现差异

如果存在阶段规划或设计文档，必须对比：

```text
Planned:
...

Actual:
...

Difference:
...

Reason:
UNKNOWN / confirmed reason
```

不要默认规划已经被实现。

# 26. 关键修改风险

基于实际代码关系，记录：

**修改哪些位置最容易影响其他系统。**

例如：

* 修改 `BuildKey` 会影响缓存命中和请求去重。
* 修改 `ActiveIndices` 更新逻辑会影响后续 Build 和 Mesh Emit。
* 修改 `Dispose()` 顺序可能导致尚未完成 Job 访问已释放内存。

这一部分必须来源于真实依赖关系。

# 28. 完整符号索引

文档末尾提供尽可能完整的符号索引。

格式：

```text
TypeName
- Path:
- Responsibility:
- Key fields:
- Key methods:
- Called by:
- Depends on:
```

对于大型模块可按目录分类。

该索引主要服务于后续 Agent 定位代码，因此宁可详细，不要追求简短。

# 29. 核心执行路径索引

最后单独总结主要执行路径。

例如：

```text
PATH A — New Build Request
Caller
→ RequestCollector.Collect()
→ FieldBuildScheduler.Enqueue()
→ FieldBuildScheduler.TrySchedule()
→ FieldBuilder.Build()
→ CommitResult()
→ RuntimeFieldStore.Update()

PATH B — Cache Hit
...
```

每个重要业务路径独立记录。

---

# 30. 未确认事项

文档最后必须存在：

## Unresolved / Uncertain

集中列出所有：

* 未确认 ownership。
* 无法追踪的动态调用。
* 可能通过反射触发的路径。
* 无法确认的生命周期。
* 缺失源码。
* 外部插件行为。
* 仅通过注释推测的信息。

**不得通过猜测消灭这一章节。**

如果完全确认，也写：

`当前扫描未发现需要标记为 UNCERTAIN 的关键事项。`

---

# 输出质量要求

生成文档前应充分扫描源码，而不是根据少数关键文件快速总结。

文档允许重复少量关键事实，因为后续 Agent 的局部检索可能只读取其中一个章节。

不要为了降低 Token 消耗删除：

* 方法职责。
* 字段状态。
* 调用关系。
* 数据所有权。
* 数据流。
* 生命周期。
* Job dependency。
* 缓存失效。
* 状态转换。
* 不变量。
* 关键实现细节。

对大型模块，宁可拆分成：

```text
ModuleArchitecture.md
ModuleDataFlow.md
ModuleAlgorithms.md
ModuleReference.md
```

也不要为了控制文档长度丢失重要事实。

---

# 最重要的禁止事项

1. 不得把理想架构写成真实架构。
2. 不得为了简洁省略实际存在的重要复杂性。
3. 不得用模糊描述代替具体类型、字段和方法。
4. 不得根据名称推测未经验证的行为。
5. 不得隐藏职责混乱、重复逻辑和技术债。
7. 不得在无法确认时自行补全。
8. 不得只描述静态类结构而忽略数据流和控制流。
9. 不得只记录 public API 而忽略影响核心行为的 private 实现。
10. 不得为了减少 Token 使用而牺牲事实完整性。
