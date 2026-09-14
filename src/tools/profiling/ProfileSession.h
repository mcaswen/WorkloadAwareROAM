#pragma once

#include <filesystem>
#include <memory>

namespace ParallelRoam::Tools::Profiling
{
/// <summary>
/// 探针独占的采集会话，负责更新窗口与外部采样器握手
/// 只允许主线程顺序调用；算法不应持有该对象或依赖采集状态
/// </summary>
class ProfileSession
{
public:
    explicit ProfileSession(const std::filesystem::path& windows);
    ~ProfileSession();
    ProfileSession(const ProfileSession&) = delete;
    ProfileSession& operator=(const ProfileSession&) = delete;

    /// <summary>
    /// 采集器就绪后才开始窗口；缺失应答、断连或超时会抛出异常
    /// 调用方应将输入输出和独立诊断放在 Begin/End 之外
    /// </summary>
    void Begin(int replay, int round);
    void End();
    void Finish();

private:
    struct State;
    std::unique_ptr<State> _state;
};
}
