#pragma once
#include "experiment/mesh_quality/MeshQualityEvaluator.h"
#include <filesystem>
#include <fstream>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 限量保存均匀序号点和全域实际最大见证，不改变评价器的全样本统计
/// 稀疏点只用于画图，Dmax仍使用独立保存的完整逐点误差
/// </summary>
class QualityPointRecorder
{
public:
    QualityPointRecorder(const std::filesystem::path& output,std::size_t expected,
        std::uint32_t width,std::uint32_t height);
    void Observe(const MeshQuality::QualityPoint& point);
    void Finish();
private:
    void Write(const MeshQuality::QualityPoint& point,const char* role);
    std::ofstream _out;
    std::size_t _stride;
    std::uint32_t _width,_height;
    MeshQuality::QualityPoint _screen,_heightPoint;
    bool _hasScreen{},_hasHeight{};
};
}
