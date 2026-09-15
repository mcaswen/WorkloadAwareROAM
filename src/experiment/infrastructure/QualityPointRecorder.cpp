#include "experiment/infrastructure/QualityPointRecorder.h"
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace ParallelRoam::Experiment::Infrastructure
{
QualityPointRecorder::QualityPointRecorder(const std::filesystem::path& output,std::size_t expected,
    std::uint32_t width,std::uint32_t height)
    :_out(output),_stride(std::max(std::size_t{1},(expected+49999)/50000)),_width(width),_height(height)
{
    _out.exceptions(std::ios::badbit|std::ios::failbit);
    _out<<std::setprecision(17)<<"ordinal,u,v,pixelX,pixelY,referenceHeight,measuredHeight,heightError,screenError,visible,role\n";
}
void QualityPointRecorder::Write(const MeshQuality::QualityPoint& p,const char* role)
{
    const auto pixel=p.ReferenceVisible ?
        glm::dvec2{(p.ReferenceClip.x/p.ReferenceClip.w+1)*_width*.5,
                   (1-p.ReferenceClip.y/p.ReferenceClip.w)*_height*.5} :
        glm::dvec2{std::numeric_limits<double>::quiet_NaN()};
    _out<<p.Ordinal<<','<<p.Uv.x<<','<<p.Uv.y<<','<<pixel.x<<','<<pixel.y<<','
        <<p.ReferenceHeight<<','<<p.MeasuredHeight<<','<<p.HeightError<<','<<p.ScreenError<<','
        <<p.ReferenceVisible<<','<<role<<'\n';
}
void QualityPointRecorder::Observe(const MeshQuality::QualityPoint& point)
{
    if(point.Ordinal%_stride==0) Write(point,"sample");
    if(std::isfinite(point.ScreenError) && point.ScreenError>=0 &&
        (!_hasScreen || point.ScreenError>_screen.ScreenError)) { _screen=point;_hasScreen=true; }
    if(std::isfinite(point.HeightError) && (!_hasHeight || point.HeightError>_heightPoint.HeightError))
        { _heightPoint=point;_hasHeight=true; }
}
void QualityPointRecorder::Finish()
{
    if(_hasScreen) Write(_screen,"screen-witness");
    if(_hasHeight) Write(_heightPoint,"height-witness");
    _out.flush();
}
}
