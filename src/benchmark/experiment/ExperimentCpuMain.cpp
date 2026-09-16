#include "benchmark/experiment/ExperimentReplay.h"
#include "benchmark/experiment/TransactionalRecoveryTrace.h"
#include <string_view>
int main(int argc,char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--recovery-trace")
    {
        return ParallelRoam::Benchmark::Experiment::RunTransactionalRecoveryTrace(argc, argv);
    }
    return ParallelRoam::Benchmark::Experiment::RunExperimentCpu(argc,argv);
}
