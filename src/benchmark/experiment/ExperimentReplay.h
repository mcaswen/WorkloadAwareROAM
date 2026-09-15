#pragma once
#include "experiment/infrastructure/ExperimentCase.h"
#include "experiment/infrastructure/CameraSequence.h"
#include "algorithms/ITerrainLodAlgorithm.h"
#include <fstream>

namespace ParallelRoam::Benchmark::Experiment
{
/// <summary>
/// 启动时核对输入并冻结公共设置，运行循环不再解析文件
/// 相机与源样本身份分别检查，图形和CPU入口共用同一任务定义
/// </summary>
struct ReplayInput
{
    ParallelRoam::Experiment::Infrastructure::ExperimentCase Case;
    ParallelRoam::Experiment::Infrastructure::CameraSequence Cameras;
    Terrain::HeightMap Source;
    Algorithms::TerrainLodSettings Settings;
    Algorithms::TerrainLodAlgorithmId Algorithm{};
};
[[nodiscard]] ReplayInput LoadReplayInput(const std::filesystem::path& path);
[[nodiscard]] bool IsEvidenceFrame(const ReplayInput& input, std::size_t frame);

/// <summary>
/// 只记录已经测得的互斥边界，证据成本与正常执行分开
/// 无图形的CPU运行将图形字段留空，不伪造零耗时
/// </summary>
struct ReplayTiming
{
    double Begin{}, Wait{}, Render{}, Present{}, Frame{}, Evidence{};
    bool Platform{};
};
/// <summary>
/// 把三种算法的公共统计写入同一列契约，缺失阶段保持空值
/// </summary>
class ReplayFrameWriter
{
public:
    explicit ReplayFrameWriter(const std::filesystem::path& output);
    void Append(const ReplayInput& input, std::size_t frame, bool zeroToOne,
        const Algorithms::TerrainLodStats& stats, const Terrain::TerrainMeshData& mesh,
        std::uint64_t meshHash, const ReplayTiming& timing,
        const std::string& image, const std::string& artifact);
private:
    std::ofstream _stream;
};
std::uint64_t ValidateReplayMesh(const Terrain::TerrainMeshData& mesh, const ReplayInput& input);
int RunExperimentPlatform(int argc,char** argv);
int RunExperimentCpu(int argc,char** argv);
}
