#include "benchmark/experiment/ExperimentReplay.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Experiment
{
ReplayInput LoadReplayInput(const std::filesystem::path& path)
{
    namespace Data=ParallelRoam::Experiment::Infrastructure;
    ReplayInput input;
    input.Case=Data::ExperimentCase::LoadResolved(path);
    const auto& c=input.Case;
    std::string error;
    if (!input.Source.LoadFromFile(c.HeightMapPath,&error)) throw std::runtime_error(error);
    if (input.Source.Width()!=static_cast<int>(c.Width) || input.Source.Height()!=static_cast<int>(c.Height))
        throw std::runtime_error("实际高度图尺寸不符");
    const auto append=[](std::uint64_t& hash,std::uint8_t byte) { hash^=byte;hash*=1099511628211ULL; };
    std::uint64_t sampleHash=14695981039346656037ULL;
    for (const auto value:input.Source.RawSamples())
    {
        append(sampleHash,static_cast<std::uint8_t>(value&255));
        append(sampleHash,static_cast<std::uint8_t>(value>>8));
    }
    if (sampleHash!=c.SampleFnv64) throw std::runtime_error("实际C++源样本与resolved内容不符");
    std::ifstream cameraBytes(c.CameraFile,std::ios::binary);
    if (!cameraBytes) throw std::runtime_error("缺少冻结相机文件");
    std::uint64_t cameraHash=14695981039346656037ULL;
    for (std::istreambuf_iterator<char> i(cameraBytes),end;i!=end;++i) append(cameraHash,static_cast<std::uint8_t>(*i));
    if (cameraHash!=c.CameraFnv64) throw std::runtime_error("相机文件内容不符");
    input.Cameras=Data::LoadCameraSequence(c.CameraFile);
    if (input.Cameras.front().Width!=static_cast<int>(c.ViewWidth) ||
        input.Cameras.front().Height!=static_cast<int>(c.ViewHeight)) throw std::runtime_error("配置视口与路线不同");
    if (input.Cameras.size()>c.MaxFrames) input.Cameras.resize(c.MaxFrames);
    if (c.Warmup>=input.Cameras.size()) throw std::runtime_error("预热覆盖全部路线");
    const auto algorithm=Algorithms::ParseTerrainLodAlgorithm(c.Algorithm);
    if (!algorithm) throw std::runtime_error("算法未启用");
    input.Algorithm=*algorithm;
    if (c.HeightPolicy!="fit" && c.Algorithm!="transactional") throw std::runtime_error("高度策略与算法不符");
    auto& s=input.Settings;
    s.EnablePassEvidence=false;
    s.TerrainSize=c.TerrainSize;s.HeightScale=c.HeightScale;s.MaxDepth=static_cast<int>(c.MaxDepth);
    s.TriangleBudget=c.Budget;s.ScreenSpaceSplitThresholdPixels=c.SplitPixels;
    s.ScreenSpaceMergeThresholdPixels=c.MergePixels;
    if (input.Algorithm == Algorithms::TerrainLodAlgorithmId::Cbt2024)
    {
        s.Cbt.Capacity = static_cast<Algorithms::TerrainLodCbtCapacity>(c.CbtCapacity);
        s.Cbt.TriangleAreaPixels = c.CbtArea;
        if (c.CbtValidation == "delayed")
        {
            s.Cbt.ValidationMode = Algorithms::TerrainLodCbtValidationMode::Delayed;
        }
        else if (c.CbtValidation == "blocking")
        {
            s.Cbt.ValidationMode = Algorithms::TerrainLodCbtValidationMode::BlockingSmoke;
        }
        s.Cbt.GeometryMode = c.CbtGeometry == "full" ?
            Algorithms::TerrainLodCbtGeometryMode::FullDebug : Algorithms::TerrainLodCbtGeometryMode::ModifiedOnly;
    }
    s.Transactional.WorkerCount=c.Workers;
    s.Transactional.PreserveSurvivingHeights=c.HeightPolicy=="immutable";
    s.Transactional.EnableFlipRecovery=c.FlipRecovery;
    s.Transactional.EnableBoundaryRefinement=c.BoundaryRefinement;
    s.Transactional.ReceiverOrder = c.ReceiverOrder == "error-first" ?
        Algorithms::TransactionalReceiverOrder::ErrorFirst : Algorithms::TransactionalReceiverOrder::Composite;
    s.Transactional.PrefixLimit=s.Transactional.DonorLimit=c.Prefix=="fixed64" ? 64 : 64*c.Budget/20000;
    if (s.Transactional.PrefixLimit==0) throw std::runtime_error("增长额度为零，请选择适用预算");
    auto& p=s.PassPolicy;
    p.SplitScoreWorkerCount=p.MergeScoreWorkerCount=p.SplitTopologyWorkerCount=
        p.MergeTopologyWorkerCount=p.MeshEmitWorkerCount=c.Workers;
    return input;
}
bool IsEvidenceFrame(const ReplayInput& input,std::size_t frame)
{
    // 头部、返回反例见证与尾部始终保留；stride只控制其余画面密度
    return frame%input.Case.CaptureStride==0 || frame==2 || frame==15 || frame==16 || frame+1==input.Cameras.size();
}
std::uint64_t ValidateReplayMesh(const Terrain::TerrainMeshData& mesh,const ReplayInput& input)
{
    const bool cbt = input.Algorithm == Algorithms::TerrainLodAlgorithmId::Cbt2024;
    const std::size_t limit = cbt ? input.Case.CbtCapacity + 6U : input.Case.Budget;
    if (mesh.Indices.empty() || mesh.Indices.size()%3 || mesh.Indices.size()/3>limit)
        throw std::runtime_error("实际网格为空或越预算");
    const bool transactional=input.Algorithm==Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod;
    for (std::size_t i=0;i<mesh.Indices.size();i+=3)
    {
        std::array<glm::dvec3,3> triangle;
        for (std::size_t j=0;j<3;++j)
        {
            if (mesh.Indices[i+j]>=mesh.Vertices.size()) throw std::runtime_error("网格索引越界");
            triangle[j]=glm::dvec3(mesh.Vertices[mesh.Indices[i+j]].Position);
            for (int axis=0;axis<3;++axis) if (!std::isfinite(triangle[j][axis])) throw std::runtime_error("网格非有限");
        }
        const double orientation=glm::cross(triangle[1]-triangle[0],triangle[2]-triangle[0]).y;
        if (!(transactional ? orientation<0 : orientation>0)) throw std::runtime_error("网格倒置或退化");
    }
    if (cbt)
    {
        // 源高度核对只作诊断；独立质量仍对完整捕获网格评价
        const float tolerance = 2.0e-5F * std::max(input.Case.HeightScale, 1.0F);
        for (const auto& vertex : mesh.Vertices)
        {
            const auto uv = vertex.TexCoord;
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y) ||
                uv.x < -1.0e-6F || uv.y < -1.0e-6F || uv.x > 1.000001F || uv.y > 1.000001F)
            {
                throw std::runtime_error("CBT实际顶点UV超出源域");
            }
            const float height = input.Source.SampleBilinear(uv.x, uv.y) * input.Case.HeightScale;
            if (std::abs(vertex.Position.y - height) > tolerance)
            {
                throw std::runtime_error("CBT实际顶点与CPU源高度诊断不符");
            }
        }
    }
    return ParallelRoam::Experiment::MeshQuality::PlatformMeshHash(mesh);
}
ReplayFrameWriter::ReplayFrameWriter(const std::filesystem::path& output):_stream(output),
    _flip(output.parent_path()/"flip-recovery.csv"),
    _boundary(output.parent_path()/"boundary-refinement.csv")
{
    _stream.exceptions(std::ios::badbit|std::ios::failbit);
    _flip.exceptions(std::ios::badbit|std::ios::failbit);
    _flip << "frame,triggered,attempts,certified,conflicts,executed\n";
    _boundary.exceptions(std::ios::badbit | std::ios::failbit);
    _boundary << "frame,attempts,certified,resolutionRejected,conflicts,freeExecuted,pairedExecuted,"
        "assignedFaces,consumedFreeFaces,unusedFaces,releasedFaces,netFaceChange\n";
    _stream << std::setprecision(17)
        << "frame,sample,event,warmup,poseHash,projectionHash,faces,budget,workers,sequence,hash,cpuMs,uploadMs,uploadBytes,beginMs,waitMs,renderMs,presentMs,frameMs,evidenceMs,split,merge,splitScoreMs,mergeScoreMs,splitTopologyMs,mergeTopologyMs,meshEmitMs,status,updated,cold,seedFaces,samples,raw,examined,receivers,need,feasible,exchanges,free,pairs,conflicts,donorReuse,touches,evaluations,vertexWrites,indexWrites,seedMs,initializeMs,viewMs,receiverMs,donorMs,reservationMs,topologyPrepareMs,topologyPublishMs,sampleRepairMs,meshPrepareMs,continuationMs,adapterMs,image,artifact\n";
}
void ReplayFrameWriter::Append(const ReplayInput& input,std::size_t frame,bool zeroToOne,
    const Algorithms::TerrainLodStats& s,const Terrain::TerrainMeshData& mesh,std::uint64_t hash,
    const ReplayTiming& t,const std::string& image,const std::string& artifact)
{
    const auto& f=input.Cameras.at(frame);
    auto& out=_stream;
    out << frame << ',' << f.SourceIndex << ',' << f.Event << ',' << (frame<input.Case.Warmup) << ','
        << f.PoseHash << ',' << (zeroToOne ? f.ZoHash:f.NoHash) << ',';
    const bool cbt = input.Algorithm == Algorithms::TerrainLodAlgorithmId::Cbt2024;
    if (!mesh.Indices.empty())
    {
        out << mesh.Indices.size()/3;
    }
    out << ',' << input.Case.Budget << ',';
    if (!cbt)
    {
        out << input.Case.Workers;
    }
    out << ',' << s.BuildSequence << ',';
    if (!mesh.Indices.empty())
    {
        out << hash;
    }
    out << ',' << s.CpuUpdateMilliseconds;
    if (t.Platform) out << ',' << s.CpuUploadMilliseconds << ',' << s.CpuGpuUploadBytes << ',' << t.Begin << ','
        << t.Wait << ',' << t.Render << ',' << t.Present << ',' << t.Frame;
    else out << ",,,,,,," << t.Frame;
    out << ',' << t.Evidence << ',';
    if (!cbt)
    {
        out << s.SplitCount;
    }
    out << ',';
    if (!cbt)
    {
        out << s.MergeCount;
    }
    if (!s.Transactional && !cbt)
        out << ',' << s.CpuSplitCandidateMarkMilliseconds << ',' << s.CpuMergeCandidateMarkMilliseconds << ','
            << s.CpuSplitTopologyMilliseconds << ',' << s.CpuMergeTopologyMilliseconds << ',' << s.CpuMeshEmitMilliseconds;
    else out << ",,,,,";
    if (s.Transactional)
    {
        const auto& x=*s.Transactional;
        // 净零恢复不进入预算交换分母；独立记录已有计数，避免零交换被误读为无修改
        _flip << frame << ',' << x.FlipTriggered << ',' << x.FlipAttempts << ',' << x.FlipCertified
            << ',' << x.FlipConflicts << ',' << x.FlipExecuted << '\n';
        // 边界接收消耗一面，预算旁表与原有接收数量保持不同口径
        _boundary << frame << ',' << x.BoundaryAttempts << ',' << x.BoundaryCertified << ','
            << x.BoundaryResolutionRejected << ',' << x.BoundaryConflicts << ',' << x.BoundaryFreeExecuted << ','
            << x.BoundaryPairedExecuted << ',' << x.AssignedFaces << ',' << x.ConsumedFreeFaces << ','
            << x.UnusedFaces << ',' << x.ReleasedFaces << ',' << x.NetFaceChange << '\n';
        out << ',' << static_cast<int>(x.Status) << ',' << x.Updated << ',' << x.ColdStart << ',' << x.SeedTriangles
            << ',' << x.Samples << ',' << x.RawCandidates << ',' << x.Examined << ',' << x.Receivers << ',' << x.Need
            << ',' << x.Feasible << ',' << x.Exchanges << ',' << x.FreeExecuted << ',' << x.PairChecks << ',' << x.Conflicts
            << ',' << x.DonorReuse << ',' << x.SampleTouches << ',' << x.SampleEvaluations << ',' << x.VertexWrites << ',' << x.IndexWrites
            << ',' << x.SeedMilliseconds << ',' << x.InitializeMilliseconds << ',' << x.ViewMilliseconds
            << ',' << x.ReceiverMilliseconds << ',' << x.DonorMilliseconds << ',' << x.ReservationMilliseconds
            << ',' << x.TopologyPrepareMilliseconds << ',' << x.TopologyPublishMilliseconds << ',' << x.SampleRepairMilliseconds
            << ',' << x.MeshPrepareMilliseconds << ',' << x.ContinuationPublishMilliseconds << ',' << x.AdapterMilliseconds;
    }
    else for(int i=0;i<31;++i) out << ',';
    out << ',' << image << ',' << artifact << '\n';
    out.flush();
}
}
