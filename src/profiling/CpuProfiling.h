#pragma once

#include <cstdint>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>

namespace ParallelRoam::Profiling
{
// 连接代际用于工具层识别中途断开，算法不承担采集生命周期
bool IsConnected();
std::uint64_t ConnectionIdentity();
void SetCurrentThreadName(const char* name);
}

// RAII 对象留在调用者作用域，不能藏进随即返回的计时 helper
#define ROAM_CPU_ZONE(name) ZoneScopedN(name)
#define ROAM_CPU_TEXT(text, size) ZoneText(text, size)
#define ROAM_CPU_VALUE(value) ZoneValue(value)
#define ROAM_CPU_THREAD(name) ::ParallelRoam::Profiling::SetCurrentThreadName(name)
#else
// 关闭时完全丢弃实参；昂贵表达式和线程名称都不得求值
#define ROAM_CPU_ZONE(name) ((void)0)
#define ROAM_CPU_TEXT(text, size) ((void)0)
#define ROAM_CPU_VALUE(value) ((void)0)
#define ROAM_CPU_THREAD(name) ((void)0)
#endif
