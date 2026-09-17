#include "algorithms/greedy_transactional_lod/TransactionalExecution.h"
#include "tools/CpuTaskExecutor.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template<class Action>
void Throws(Action&& action)
{
    bool failed = false;
    try
    {
        action();
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }
    Require(failed, "预期失败没有发生");
}

void UniqueClaimsAndResults(const TransactionalExecution& execution)
{
    for (const auto count : {0U, 1U, 2U, 37U, 160U})
    {
        std::vector<std::atomic<unsigned>> visits(count);
        std::vector<unsigned> expected(count), actual(count);
        WorkLedger fixed, independent;
        const auto fill = [](auto first, auto last, auto& output, WorkLedger& work) {
            for (auto index = first; index < last; ++index)
            {
                output[index] = static_cast<unsigned>(index * index + 7);
                ++work.Proposals;
                work.Touch();
                ++work.Reasons[index % 2 == 0 ? "even" : "odd"];
            }
        };
        execution.Run("fixed", count, fixed, [&](auto first, auto last, WorkLedger& local) {
            fill(first, last, expected, local);
        });
        execution.RunIndependent("independent", count, independent, [&](auto first, auto last, WorkLedger& local) {
            for (auto index = first; index < last; ++index)
            {
                ++visits[index];
            }
            fill(first, last, actual, local);
        });
        for (const auto& visit : visits)
        {
            Require(visit == 1, "独立项被漏领或重复执行");
        }
        Require(actual == expected && fixed.Proposals == independent.Proposals &&
            fixed.SampleTouches == independent.SampleTouches && fixed.Reasons == independent.Reasons,
            "动态领取改变索引输出或整数账本");
    }
}

void DrainAndReuse(const TransactionalExecution& execution)
{
    std::barrier rendezvous(4);
    WorkLedger work;
    execution.RunIndependent("threads", 4, work, [&](auto first, auto last, WorkLedger& local) {
        Require(last == first + 1, "并行独立项没有按根划分");
        rendezvous.arrive_and_wait();
        ++local.Proposals;
    });
    Require(work.Execution.at("threads")[2] == 4 && work.Proposals == 4, "没有真实四线程或账本错误");

    std::atomic<unsigned> completed{0};
    Throws([&] {
        execution.RunIndependent("failure", 12, work, [&](auto first, auto, WorkLedger&) {
            // 首轮四根会合，确保抛出时其余同步任务已经实际运行
            if (first < 4)
            {
                rendezvous.arrive_and_wait();
            }
            ++completed;
            if (first == 0)
            {
                throw std::runtime_error("独立根准备失败");
            }
        });
    });
    Require(completed == 12, "异常传播前没有排空其余独立项");
    UniqueClaimsAndResults(execution);
}

void Limits(const TransactionalExecution& execution)
{
    WorkLedger aggregate;
    aggregate.VisitLimit = 3;
    std::barrier rendezvous(4);
    std::atomic<unsigned> completed{0};
    Throws([&] {
        execution.RunIndependent("aggregate_limit", 4, aggregate, [&](auto, auto, WorkLedger& local) {
            // 每个局部只访问一次、均未超额；全批四次必须在归并后拒绝
            local.Touch();
            rendezvous.arrive_and_wait();
            ++completed;
        });
    });
    Require(completed == 4 && aggregate.SampleTouches == 4, "更细领取绕过了全批访问配额");

    WorkLedger localLimit;
    localLimit.VisitLimit = 0;
    Throws([&] {
        execution.RunIndependent("local_limit", 8, localLimit, [](auto, auto, WorkLedger& local) {
            local.Touch();
        });
    });
    WorkLedger expired;
    expired.Deadline = std::chrono::steady_clock::time_point::min();
    std::atomic<unsigned> invoked{0};
    Throws([&] {
        execution.RunIndependent("expired", 8, expired, [&](auto, auto, WorkLedger&) { ++invoked; });
    });
    Require(invoked == 0, "过期任务仍进入业务回调");
}
}

int main()
{
    try
    {
        UniqueClaimsAndResults(TransactionalExecution{});
        ParallelRoam::Tools::CpuTaskExecutor pool(4);
        TransactionalExecution execution{4, true, [&](auto count, const auto& task) { pool.Dispatch(count, task); }};
        UniqueClaimsAndResults(execution);
        DrainAndReuse(execution);
        Limits(execution);
        std::cout << "独立根领取、结果、配额与同步生命周期核查完成\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
