#include "tools/profiling/ProfileSession.h"
#include "profiling/CpuProfiling.h"

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
// 夹具直接验证官方接口的开启构建，关闭构建不引入客户端头文件
#define ZoneScopedN(name)
#define FrameMarkNamed(name)
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_MSC_VER)
#define PROFILE_FIXTURE_NOINLINE __declspec(noinline)
#else
#define PROFILE_FIXTURE_NOINLINE __attribute__((noinline))
#endif

namespace
{
// 仅解析夹具保留两个可观察调用层级，业务代码仍按正常优化处理
PROFILE_FIXTURE_NOINLINE std::uint64_t ProfileHotLeaf(std::uint64_t value, std::uint64_t iterations)
{
    ZoneScopedN("fixture.hot_leaf");
    // 夹具显式拥有栈局部量，使条件性帧指针采集也能观察父调用
    volatile std::uint64_t seed = value;
    value = seed;
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        value ^= value >> 13;
        value *= 0x9E3779B185EBCA87ULL;
        value ^= value << 7;
    }
    return value;
}

PROFILE_FIXTURE_NOINLINE std::uint64_t ProfileHotParent(std::uint64_t value, std::uint64_t iterations)
{
    ZoneScopedN("fixture.hot_parent");
    const auto result = ProfileHotLeaf(value, iterations);
    // 保留返回后的实际计算，避免尾调用消除待验证的父调用帧
    return result ^ (result >> 17);
}

bool WaitForCapture()
{
    if (std::getenv("ROAM_PROFILE_WAIT") == nullptr) return true;
#if defined(TRACY_ENABLE)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!TracyIsConnected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return TracyIsConnected;
#else
    return false;
#endif
}

void ProfileException()
{
    ZoneScopedN("fixture.exception");
    throw std::runtime_error("expected fixture exception");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
#if !defined(TRACY_ENABLE)
        int evaluated=0;
        ROAM_CPU_ZONE(++evaluated);
        ROAM_CPU_TEXT(++evaluated,++evaluated);
        ROAM_CPU_VALUE(++evaluated);
        ROAM_CPU_THREAD(++evaluated);
        if (evaluated) throw std::runtime_error("disabled profiling evaluated its arguments");
#endif
        const std::uint64_t iterations = argc == 2 ? std::stoull(argv[1]) : 200000;
        if (argc > 2 || iterations == 0 || iterations > 500000000ULL)
            throw std::runtime_error("iterations must be in [1, 500000000]");
        if (!WaitForCapture()) throw std::runtime_error("capture connection unavailable");
        std::unique_ptr<ParallelRoam::Tools::Profiling::ProfileSession> session;
        if (const auto* windows = std::getenv("ROAM_PROFILE_WINDOWS"))
            session = std::make_unique<ParallelRoam::Tools::Profiling::ProfileSession>(windows);
#if defined(TRACY_ENABLE)
        tracy::SetThreadName("fixture.main");
#endif
        std::array<std::uint64_t, 3> results{};
        if (session) session->Begin(0, 0);
        // 故障夹具允许控制端在活动窗口中断开；不进入自然算法路径
        if (const auto* hold = std::getenv("ROAM_PROFILE_HOLD_MS"))
        {
            const auto duration = std::stoul(hold);
            if (duration > 1000) throw std::runtime_error("fixture hold exceeds 1000ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
        }
        const auto start = std::chrono::steady_clock::now();
        {
            ZoneScopedN("fixture.frame");
            // 独占结果槽并在读取前 join，工具线程不能改变计算结果
            std::array<std::jthread, 2> threads;
            for (std::size_t i = 0; i < threads.size(); ++i)
            {
                threads[i] = std::jthread([&, i]
                {
#if defined(TRACY_ENABLE)
                    tracy::SetThreadName(i == 0 ? "fixture.worker.0" : "fixture.worker.1");
#endif
                    ZoneScopedN("fixture.task");
                    results[i] = ProfileHotParent(i + 1, iterations);
                });
            }
            results[2] = ProfileHotParent(3, iterations);
            {
                ZoneScopedN("fixture.wait");
                for (auto& thread : threads) thread.join();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            try { ProfileException(); }
            catch (const std::runtime_error&) { }
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (session) { session->End(); session->Finish(); }
        {
            // 此区间在全部业务区间结束后产生，导出检查用它识别尾部缺失
            ZoneScopedN("fixture.complete");
            FrameMarkNamed("fixture");
        }
        std::cout << "{\"checksum\":" << (results[0] ^ results[1] ^ results[2])
                  << ",\"iterations\":" << iterations << ",\"elapsed_ms\":" << elapsed << "}\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
