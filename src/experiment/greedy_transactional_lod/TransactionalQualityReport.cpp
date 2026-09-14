#include <algorithm>
#include "experiment/greedy_transactional_lod/TransactionalQualityReport.h"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using namespace Algorithms::GreedyTransactionalLod;
TransactionalQualitySummary TransactionalQualityReport::Summarize(const TransactionalSamples& samples)
{
    TransactionalQualitySummary result;double sum=0;
    for (Slot sid=0;sid<samples.SampleCount();++sid)
    {
        const auto value=samples.Value(sid);
        if (value.ErrorSquared>result.Screen) { result.Screen=value.ErrorSquared;result.ScreenSample=sid; }
        if (value.HeightError>result.Height) { result.Height=value.HeightError;result.HeightSample=sid; }
        if (value.Visible) { ++result.Visible;sum+=value.ErrorSquared; }
    }
    result.Screen=std::sqrt(result.Screen);
    result.Rms=result.Visible ? std::sqrt(sum/static_cast<double>(result.Visible)) : 0;
    return result;
}

void TransactionalQualityReport::Write(const TransactionalSamples& samples,const std::filesystem::path& directory)
{
    static_assert(std::endian::native==std::endian::little && sizeof(double)==8);
    const auto summary=Summarize(samples);
    std::ofstream json(directory/"quality.json");
    json<<std::setprecision(17)<<"{\"sampledScreenMaxPx\":"<<summary.Screen
        <<",\"sampledHeightMax\":"<<summary.Height<<",\"terrainSampleScreenRms\":"<<summary.Rms
        <<",\"screenSample\":"<<summary.ScreenSample<<",\"heightSample\":"<<summary.HeightSample
        <<",\"visible\":"<<summary.Visible<<",\"q\":"<<samples.SampleCount()
        <<",\"errorFormat\":\"little-endian-f64-px-invisible-minus-one\"}";
    std::ofstream errors(directory/"errors.f64",std::ios::binary);
    // 以连续块写出规范样本顺序；负值只表示参考不可见，不能参与最大差计算
    std::array<double,4096> buffer{};
    for (Slot first=0;first<samples.SampleCount();first+=buffer.size())
    {
        const auto count=std::min(buffer.size(),samples.SampleCount()-first);
        for (Slot i=0;i<count;++i)
        {
            const auto& value=samples.Projection(first+i);
            buffer[i]=value.Visible ? std::sqrt(value.ErrorSquared) : -1;
        }
        errors.write(reinterpret_cast<const char*>(buffer.data()),static_cast<std::streamsize>(count*sizeof(double)));
    }
    if (!json || !errors) throw std::runtime_error("离线质量文件写入失败");
}
}
