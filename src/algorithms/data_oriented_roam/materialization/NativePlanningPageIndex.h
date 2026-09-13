#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 32 位下标的按需页索引，页内编码零表示无覆盖，读取缺页不会分配
/// 目录按最高写入页号增长，其初始化费用不能隐藏成纯触及规模
/// </summary>
class NativePlanningPageIndex
{
public:
    static constexpr std::size_t PageSize = 256;
    explicit NativePlanningPageIndex(bool diagnostics = false) : _diagnostics(diagnostics) {}

    [[nodiscard]] std::uint32_t Find(std::uint64_t key) const
    {
        // 宽键查询可以直接判缺失，写入必须报错，不能截断后命中另一个节点
        if (key > std::numeric_limits<std::uint32_t>::max()) return 0;
        const auto page = static_cast<std::size_t>(key / PageSize);
        const auto* values = page < _directory.size() ? _directory[page].get() : nullptr;
        if (_diagnostics)
        {
            ++_metrics.Lookups;
            // 此处计索引读取层数，不是哈希探测或 CPU load 次数
            const std::size_t reads = values ? 2U : 1U;
            _metrics.Probes += reads;
            _metrics.MaximumProbe = std::max(_metrics.MaximumProbe, reads);
        }
        return values ? (*values)[static_cast<std::size_t>(key % PageSize)] : 0;
    }

    /// <summary>
    /// 仅为已经确认缺失的键准备槽位，返回的编码引用不受记录向量扩容影响
    /// recordBytes 用于记录所属存储在目录或页分配期间的同时存活字节
    /// </summary>
    std::uint32_t& Prepare(std::uint64_t key, std::size_t recordBytes)
    {
        if (key > std::numeric_limits<std::uint32_t>::max())
            throw std::out_of_range("planning page key exceeds 32 bits");
        const auto page = static_cast<std::size_t>(key / PageSize);
        if (page >= _directory.size())
        {
            if (page >= _directory.capacity())
            {
                std::size_t capacity = std::max<std::size_t>(16, _directory.capacity());
                while (capacity <= page) capacity *= 2U;
                // reserve 时新旧目录可能同时存活，页对象本身保持原址
                Peak(recordBytes + Bytes() + capacity * sizeof(PagePointer));
                _metrics.DirectoryMoved += _directory.size();
                _directory.reserve(capacity);
                ++_metrics.Allocations;
            }
            _metrics.DirectoryInitialized += page + 1U - _directory.size();
            // 跳跃写入会初始化中间的空目录项，这部分按高水位单独计费
            _directory.resize(page + 1U);
        }
        if (!_directory[page])
        {
            Peak(recordBytes + Bytes() + sizeof(Page));
            // 值初始化把整页编码清零，缺失字段仍由 View 解释，不复制来源字段
            _directory[page] = std::make_unique<Page>();
            ++_metrics.IndexPages; ++_metrics.Allocations;
            _metrics.InitializedSlots += PageSize;
        }
        return (*_directory[page])[static_cast<std::size_t>(key % PageSize)];
    }

    [[nodiscard]] std::size_t Bytes() const noexcept
    { return _directory.capacity() * sizeof(PagePointer) + _metrics.IndexPages * sizeof(Page); }
    [[nodiscard]] NativePlanningStorageMetrics Metrics() const
    {
        auto result = _metrics;
        result.PagedIndex = true;
        result.IndexCapacity = result.IndexPages * PageSize;
        result.IndexBytes = sizeof(std::uint32_t);
        result.DirectorySize = _directory.size(); result.DirectoryCapacity = _directory.capacity();
        result.ReservedBytes = Bytes();
        return result;
    }

private:
    using Page = std::array<std::uint32_t, PageSize>;
    using PagePointer = std::unique_ptr<Page>;
    void Peak(std::size_t bytes) { _metrics.PeakReservedBytes = std::max(_metrics.PeakReservedBytes, bytes); }
    bool _diagnostics;
    // 调用退出时遍历目录并释放已分配页，销毁费用包含空目录项检查
    std::vector<PagePointer> _directory;
    mutable NativePlanningStorageMetrics _metrics;
};
}
