#include "profiling/CpuProfiling.h"

namespace ParallelRoam::Profiling
{
bool IsConnected() { return TracyIsConnected; }
std::uint64_t ConnectionIdentity() { return tracy::GetProfiler().ConnectionId(); }
void SetCurrentThreadName(const char* name) { tracy::SetThreadName(name); }
}
