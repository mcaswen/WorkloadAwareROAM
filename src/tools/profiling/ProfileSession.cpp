#include "ProfileSession.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#if defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#endif

namespace ParallelRoam::Tools::Profiling
{
namespace
{
std::uint64_t NowNanoseconds()
{
#if defined(__linux__)
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("profiling monotonic clock unavailable");
    return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(value.tv_nsec);
#else
    throw std::runtime_error("perf session requires Linux");
#endif
}
}

/// <summary>
/// 控制描述符和窗口日志具有同一会话生命周期，异常时也必须关闭
/// 采样器退出后仍保留本地 FIFO 端点，以超时报告失败，避免触发 SIGPIPE
/// </summary>
struct ProfileSession::State
{
    std::ofstream Windows;
    int Control{-1}, Acknowledgment{-1};
    int Replay{}, Round{};
    std::uint64_t Start{}, EnableCost{};
    bool Active{}, Finished{}, MayConsumeAckNull{};

    ~State()
    {
#if defined(__linux__)
        if (Control >= 0) close(Control);
        if (Acknowledgment >= 0) close(Acknowledgment);
#endif
    }

    void Exchange(const char* operation)
    {
#if defined(__linux__)
        const std::string request = std::string(operation) + '\n';
        ssize_t written;
        do { written = write(Control, request.data(), request.size()); }
        while (written < 0 && errno == EINTR);
        if (written != static_cast<ssize_t>(request.size()))
            throw std::runtime_error("perf control write failed");

        // 命令只有一个在途应答，不能把旧 ack 误认为下一窗口已经启用
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string response;
        while (std::chrono::steady_clock::now() < deadline)
        {
            pollfd item{Acknowledgment, POLLIN, 0};
            const int ready = poll(&item, 1, 50);
            if (ready < 0 && errno != EINTR) throw std::runtime_error("perf ack poll failed");
            if (ready <= 0) continue;
            char bytes[32];
            const auto count = read(Acknowledgment, bytes, sizeof(bytes));
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (count <= 0) throw std::runtime_error("perf ack read failed");
            response.append(bytes, static_cast<std::size_t>(count));
            // 已冻结 perf 会写结尾 NUL；兼容不含 NUL 的实现及分段读取
            if (MayConsumeAckNull && !response.empty())
            {
                if (response.front() == '\0') response.erase(0, 1);
                MayConsumeAckNull = false;
            }
            if (response == "ack\n") { MayConsumeAckNull = true; return; }
            if (response == std::string("ack\n\0", 5)) return;
            if (response.size() >= 4) throw std::runtime_error("perf ack protocol mismatch");
        }
        throw std::runtime_error("perf control acknowledgment timed out");
#else
        static_cast<void>(operation);
        throw std::runtime_error("perf session requires Linux");
#endif
    }
};

ProfileSession::ProfileSession(const std::filesystem::path& windows) : _state(std::make_unique<State>())
{
#if defined(__linux__)
    const auto* control = std::getenv("ROAM_PERF_CONTROL");
    const auto* ack = std::getenv("ROAM_PERF_ACK");
    if (!control || !ack) throw std::runtime_error("missing perf control environment");
    _state->Control = open(control, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    _state->Acknowledgment = open(ack, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (_state->Control < 0 || _state->Acknowledgment < 0)
        throw std::runtime_error("cannot open perf control channels");
    if (std::filesystem::exists(windows)) throw std::runtime_error("refusing existing profile windows");
    _state->Windows.open(windows);
    _state->Windows << "replay,round,start_ns,end_ns,enable_ns,disable_ns\n";
    if (!_state->Windows) throw std::runtime_error("cannot create profile windows");
#else
    static_cast<void>(windows);
    throw std::runtime_error("perf session requires Linux");
#endif
}

ProfileSession::~ProfileSession()
{
    if (_state->Active)
    {
        // 异常路径尝试停采，但不能以析构异常遮蔽原始算法错误
        try { _state->Exchange("disable"); } catch (...) { }
    }
}

void ProfileSession::Begin(int replay, int round)
{
    if (_state->Active || _state->Finished) throw std::runtime_error("invalid profiling begin");
    const auto started = NowNanoseconds();
    _state->Exchange("enable");
    _state->Start = NowNanoseconds();
    _state->EnableCost = _state->Start - started;
    _state->Replay = replay;
    _state->Round = round;
    _state->Active = true;
}

void ProfileSession::End()
{
    if (!_state->Active) throw std::runtime_error("profiling window is not active");
    const auto ended = NowNanoseconds();
    _state->Exchange("disable");
    const auto disabled = NowNanoseconds();
    _state->Active = false;
    _state->Windows << _state->Replay << ',' << _state->Round << ',' << _state->Start << ',' << ended
                    << ',' << _state->EnableCost << ',' << disabled - ended << '\n';
    _state->Windows.flush();
    if (!_state->Windows) throw std::runtime_error("profile window write failed");
}

void ProfileSession::Finish()
{
    if (_state->Active || _state->Finished) throw std::runtime_error("invalid profiling finish");
    _state->Windows << "# complete\n";
    _state->Windows.flush();
    if (!_state->Windows) throw std::runtime_error("profile completion write failed");
    _state->Finished = true;
}
}
