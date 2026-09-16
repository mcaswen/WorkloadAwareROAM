#pragma once

namespace ParallelRoam::Benchmark::Experiment
{
/// <summary>
/// 复用冻结实验输入执行有限质量追溯，仅供独立 CPU 研究入口调用
/// </summary>
int RunTransactionalRecoveryTrace(int argc, char** argv);
}
