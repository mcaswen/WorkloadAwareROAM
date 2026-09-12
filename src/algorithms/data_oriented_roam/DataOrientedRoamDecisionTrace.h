#pragma once

#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 严格收敛的停止语义由新旧控制器共享，判定过程各自独立
/// </summary>
enum class TopologySplitStop { NotStarted, Running, NoEligibleSplit, BudgetBlocked, IterationLimit };

/// <summary>
/// 严格决策的逻辑事件，不包含物理堆交换、节点槽位或访问足迹
/// </summary>
enum class DecisionEventKind
{
    SelectSplit, SelectMerge, AttemptBegin, AttemptEnd, Reserve, Release,
    SplitComplete, MergeComplete, SplitRootEnd, MergeRootEnd, BlockSplit, RemoveMerge, Stop
};

/// <summary>
/// 仅编码真实控制分支；强制来源由 From 单独表示，不从请求排序推测
/// </summary>
enum class DecisionReason
{
    None, Requested, Forced, LowScoreMerge, BudgetExchange,
    InvalidNode, DepthLimit, BudgetRejected, PrerequisiteFailed, Success, Failure
};

/// <summary>
/// 以稳定身份和预算账本比较两份独立控制器，存储由测试调用方负责
/// Stop 的 Value 是停止枚举，Limit 是入口冻结的迭代上限
/// </summary>
struct DecisionEvent
{
    DecisionEventKind Kind{};
    DecisionReason Reason{DecisionReason::None};
    std::size_t Iteration{0}, Before{0}, After{0}, Value{0}, Limit{0};
    std::uint64_t Root{0}, Node{0}, From{0};
    float Score{0};
    bool operator==(const DecisionEvent&) const = default;
};

/// <summary>
/// 同步诊断接收器不拥有存储，回调不得抛出异常或修改被观察状态
/// 容量不足由接收方记为审计未完成，不用截断轨迹宣称等价
/// </summary>
struct DecisionTraceSink
{
    void* Context{nullptr};
    void (*Append)(void*, const DecisionEvent&){nullptr};
};

/// <summary>
/// 只保存当前递归上下文，普通生产模板不创建或访问该游标
/// </summary>
struct DecisionTraceCursor
{
    DecisionTraceSink Sink;
    std::size_t Iteration{0};
    std::uint64_t Root{0}, Node{0}, From{0};

    void Emit(DecisionEventKind kind, DecisionReason reason = DecisionReason::None,
        std::size_t before = 0, std::size_t after = 0, std::size_t value = 0,
        float score = 0, std::size_t limit = 0) const
    {
        // 接收方得到值拷贝，不能通过事件保留当前节点池或堆的借用地址
        if (Sink.Append) Sink.Append(Sink.Context,
            {kind, reason, Iteration, before, after, value, limit, Root, Node, From, score});
    }

    void Select(std::uint64_t path)
    {
        Root = Node = path;
        From = 0;
    }
};

/// <summary>
/// 递归返回时恢复父尝试的预算归属，所有失败出口都有对应完成事件
/// 关闭实例不读取游标，编译器可移除整个观察作用域
/// </summary>
template<bool Enabled>
class DecisionAttemptScope
{
public:
    DecisionAttemptScope(DecisionTraceCursor* trace, std::uint64_t path,
        std::uint64_t from, bool forced) : _trace(trace)
    {
        if constexpr (Enabled)
        {
            _oldNode = trace->Node; _oldFrom = trace->From;
            // Root 保持最外层请求身份，Node/From 随递归变化，用于区分预算归属
            trace->Node = path; trace->From = from;
            trace->Emit(DecisionEventKind::AttemptBegin, forced ? DecisionReason::Forced : DecisionReason::Requested);
        }
    }
    // 复制作用域会为同一次尝试重复发出完成事件，因此禁止复制和赋值
    DecisionAttemptScope(const DecisionAttemptScope&) = delete;
    DecisionAttemptScope& operator=(const DecisionAttemptScope&) = delete;
    ~DecisionAttemptScope()
    {
        if constexpr (Enabled)
        {
            _trace->Emit(DecisionEventKind::AttemptEnd, Result);
            // 完成事件先使用当前尝试身份，随后父请求的释放才能归回父层
            _trace->Node = _oldNode; _trace->From = _oldFrom;
        }
    }
    DecisionReason Result{DecisionReason::InvalidNode};

private:
    DecisionTraceCursor* _trace;
    std::uint64_t _oldNode{0}, _oldFrom{0};
};
}
