#include "benchmark/experiment/ExperimentVisualArtifacts.h"
#include <fstream>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Experiment
{
void WriteCapturedFrame(const std::filesystem::path& path, const Render::FrameCapture& capture)
{
    if (std::filesystem::exists(path)) throw std::runtime_error("截图文件已存在");
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << "P6\n" << capture.Width << " " << capture.Height << "\n255\n";
    for (std::size_t i = 0; i < capture.Rgba.size(); i += 4)
        stream.write(reinterpret_cast<const char*>(capture.Rgba.data() + i), 3);
}
}
