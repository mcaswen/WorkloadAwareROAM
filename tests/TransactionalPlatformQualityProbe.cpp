#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "experiment/mesh_quality/MeshQualityEvaluator.h"
#include "experiment/infrastructure/QualityPointRecorder.h"
#include <memory>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv)
{
    using namespace ParallelRoam;
    using namespace Experiment::MeshQuality;
    try
    {
        if (argc!=4 && !(argc==5 && std::string_view(argv[4])=="--locations"))
            throw std::runtime_error("Usage: quality-probe MESH HEIGHTMAP OUTPUT_DIRECTORY [--locations]");
        auto artifact=ReadPlatformMesh(argv[1]);Terrain::HeightMap height;std::string error;
        if (!height.LoadFromFile(argv[2],&error)) throw std::runtime_error(error);
        const std::filesystem::path output(argv[3]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Refusing quality overwrite");
        std::filesystem::create_directories(output);
        const auto& mesh=artifact.Mesh;
        // 规则网格仅定义独立采样域，参考高度仍来自 raw 双线性插值
        const auto domain=Terrain::TerrainMeshBuilder::Build(height,mesh.TerrainSize,mesh.HeightScale);
        const BilinearHeightfieldReference source{height.RawSamples(),height.Width(),height.Height(),mesh.TerrainSize,mesh.HeightScale};
        std::vector<double> errors;QualityOptions options;options.MaximumSeconds=120;options.PointErrors=&errors;
        const QualityView view{artifact.Matrix,artifact.Width,artifact.Height,artifact.ZeroToOne!=0};
        std::unique_ptr<Experiment::Infrastructure::QualityPointRecorder> locations;
        if(argc==5)
        {
            const auto n=static_cast<std::size_t>(height.Width()-1);
            locations=std::make_unique<Experiment::Infrastructure::QualityPointRecorder>(
                output/"locations.csv",6*n*n+4*n+1,artifact.Width,artifact.Height);
            options.PointObserver=[&](const auto& point){ locations->Observe(point); };
        }
        const auto result=EvaluateMeshQuality(source,domain,mesh,view,options);
        if(locations) locations->Finish();
        std::ofstream json(output/"quality.json");json.exceptions(std::ios::badbit|std::ios::failbit);json<<std::setprecision(17);
        const auto optional=[&](std::optional<double> value) { if (value) json<<*value;else json<<"null"; };
        json<<"{\"status\":\""<<ToString(result.Status)<<"\",\"meshHash\":\""<<PlatformMeshHash(mesh)
            <<"\",\"sampleHash\":\""<<result.SampleHash<<"\",\"q\":"<<result.SampleCount
            <<",\"visible\":"<<result.ScreenSampleCount<<",\"missing\":"<<result.MissingCoverageCount
            <<",\"ambiguous\":"<<result.AmbiguousCoverageCount<<",\"nearCrossing\":"<<result.NearPlaneCrossingCount
            <<",\"invalidGeometry\":"<<result.InvalidGeometryCount<<",\"invalidProjection\":"<<result.InvalidProjectionCount
            <<",\"screenMax\":";optional(result.SampledScreenMaxPx);
        json<<",\"heightMax\":";optional(result.SampledHeightMax);json<<",\"terrainSampleRms\":";optional(result.TerrainSampleScreenRms);
        json<<",\"screenWitness\":["<<result.ScreenMaximum.SampleOrdinal<<','<<result.ScreenMaximum.Uv.x<<','<<result.ScreenMaximum.Uv.y
            <<"],\"heightWitness\":["<<result.HeightMaximum.SampleOrdinal<<','<<result.HeightMaximum.Uv.x<<','<<result.HeightMaximum.Uv.y
            <<"],\"offlineMs\":"<<result.TotalMilliseconds<<'}';
        std::ofstream points(output/"errors.f64",std::ios::binary);points.exceptions(std::ios::badbit|std::ios::failbit);
        points.write(reinterpret_cast<const char*>(errors.data()),static_cast<std::streamsize>(errors.size()*sizeof(double)));
        std::cout<<ToString(result.Status)<<" q="<<result.SampleCount<<" ms="<<result.TotalMilliseconds<<'\n';
        return result.Status==EvaluationStatus::Sampled ? 0 : 1;
    }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 2; }
}
