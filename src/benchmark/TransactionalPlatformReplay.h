#pragma once

namespace ParallelRoam::Benchmark
{
/// <summary>
/// 执行冻结的有限图形回放；输出身份和文件写入位于计时之后
/// </summary>
int RunTransactionalPlatformReplay(int argc,char** argv);
}
