#pragma once
#include "experiment/infrastructure/CameraSequence.h"

namespace ParallelRoam::Benchmark::Experiment
{
[[nodiscard]] ParallelRoam::Experiment::Infrastructure::CameraSequence
    ExportLegacyCamera(const std::string& id, bool peking);
int RunExperimentCameraPreview(int argc, char** argv);
int RunExperimentCameraTools(int argc, char** argv);
}
