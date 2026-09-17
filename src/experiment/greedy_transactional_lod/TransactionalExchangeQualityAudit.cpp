#include "experiment/greedy_transactional_lod/TransactionalExchangeQualityAudit.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Clock = std::chrono::steady_clock;

/// <summary>
/// 采集账本与生产账本分离；全根扫描和局部闭支持访问分别计费
/// </summary>
struct CaptureWork
{
    std::size_t Associations{}, SampleRecords{}, Visible{}, RootContributions{}, SourceValues{};
    double SourceSeconds{}, RootsSeconds{}, LocalSeconds{};
};

void Number(std::ostream& output, double value)
{
    // JSON 不接受 NaN/Inf；保留 null，让独立检查明确归入数值未知
    if (std::isfinite(value))
    {
        output << value;
    }
    else
    {
        output << "null";
    }
}

/// <summary>
/// 旧补丁取完整面几何；新补丁直接导出获批记录，不读取 HeightProof
/// 样本坐标随后用整数比恢复，不使用近似参数坐标充当精确输入
/// </summary>
void Geometry(std::ostream& output, const std::map<Identity, Point>& points,
    const std::vector<std::array<Identity, 3>>& faces)
{
    output << "{\"points\":[";
    // 逻辑身份随坐标一起保存，不能靠输出行号重建共享顶点
    bool first = true;
    for (const auto& [id, point] : points)
    {
        if (!first)
        {
            output << ',';
        }
        first = false;
        output << '[' << id << ',';
        Number(output, point.U);
        output << ',';
        Number(output, point.V);
        output << ',';
        Number(output, point.Height);
        output << ']';
    }
    output << "],\"faces\":[";
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        const auto& face = faces[i];
        output << (i == 0 ? "" : ",") << '[' << face[0] << ',' << face[1] << ',' << face[2] << ']';
    }
    output << "]}";
}

std::set<Slot> ClosedSamples(const TransactionalSamples& samples, const Proposal& proposal,
    CaptureWork& work)
{
    std::set<Slot> result;
    // owner 可在补丁外，闭边样本仍影响本补丁证据，不能按 owner 过滤
    for (const auto face : proposal.Support)
    {
        const auto& associations = samples.FaceSamples(face);
        work.Associations += associations.size();
        result.insert(associations.begin(), associations.end());
    }
    return result;
}

void Side(std::ostream& output, const TransactionalState& state, const TransactionalSamples& samples,
    const Proposal& proposal, const std::set<Slot>& closed, CaptureWork& work)
{
    std::map<Identity, Point> points;
    std::vector<std::array<Identity, 3>> faces;
    // 不借用缓存的可见提案样本，离屏 donor 也必须导出完整闭支持
    for (const auto slot : proposal.Support)
    {
        const auto& face = state.Face(slot).Vertices;
        faces.push_back(face);
        for (const auto id : face)
        {
            points.emplace(id, state.Vertex(id).Geometry);
        }
    }
    output << "{\"kind\":\"" << proposal.Kind << "\",\"root\":";
    // donor 不一定有接收根，缺失身份与合法的零号根必须分开
    if (proposal.Root == InvalidSlot)
    {
        output << "null";
    }
    else
    {
        output << state.Face(proposal.Root).Id;
    }
    output << ",\"center\":" << proposal.Center << ",\"newVertex\":" << proposal.NewVertex << ",\"old\":";
    Geometry(output, points, faces);
    output << ",\"new\":";
    // 保留实际获批高度；这里不重新调用源高查询或拟合以修饰旧提案
    Geometry(output, proposal.Points, proposal.Faces);
    output << ",\"samples\":[";
    bool first = true;
    for (const auto id : closed)
    {
        const auto xy = samples.Decode(id);
        // 可见性与几何缓存同属当前视图，导出后由独立公式核对版本对应
        const auto value = samples.Value(id);
        if (!first)
        {
            output << ',';
        }
        first = false;
        // 最后三列仅供诊断交叉核查，不参与独立有理接受判定
        output << '[' << id << ',' << xy[0] << ',' << xy[1] << ',' << value.Visible << ',';
        Number(output, value.MeshHeight);
        output << ',';
        Number(output, value.ReferenceHeight);
        output << ',';
        Number(output, value.ErrorSquared);
        output << ']';
        work.Visible += value.Visible ? 1 : 0;
    }
    output << "]}";
}

/// <summary>
/// 全根目标人口仅供请求缺口审计，扫描费用单列，不能冒充局部更新成本
/// 资格按当前政策记录，旧P人口与显式质量目标人口不能混为一谈
/// </summary>
void Roots(const TransactionalState& state, const TransactionalSamples& samples,
    const std::filesystem::path& path, CaptureWork& work)
{
    std::ofstream output(path);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << std::setprecision(17) << "root,errorSquared,eligible,inPrefix,visibleSamples,contributions\n";
    const auto prefix = samples.Prefix(state.Config().PrefixLimit);
    // 完整根人口包含旧资格外成员，不能只遍历 Raw 或 Prefix 自证覆盖
    const std::set<Slot> selected(prefix.begin(), prefix.end());
    for (const auto face : state.ActiveFaces())
    {
        double maximum = 0;
        std::size_t visible = 0;
        // 空可见支持保留独立计数，零最大值不代表全闭域几何没有损伤
        for (const auto sample : samples.FaceSamples(face))
        {
            const auto& projection = samples.Projection(sample);
            if (projection.Visible)
            {
                maximum = std::max(maximum, projection.ErrorSquared);
                ++visible;
            }
        }
        const auto count = samples.FaceSamples(face).size();
        work.RootContributions += count;
        const double priority = samples.PrioritySquared(face);
        const auto& config = state.Config();
        const bool target = config.QualityPolicy == Algorithms::TransactionalQualityPolicy::PointwiseTarget;
        const bool eligible = target ? std::isfinite(maximum) && maximum > config.QualityTargetPixels * config.QualityTargetPixels :
            std::isfinite(priority) && priority > config.SplitPixels * config.SplitPixels;
        output << state.Face(face).Id << ',' << maximum << ',' << eligible << ',' << selected.contains(face)
            << ',' << visible << ',' << count << '\n';
    }
}
}

void TransactionalExchangeQualityAudit::Capture(const TransactionalState& state,
    const TransactionalSamples& samples, const CertifiedBatch& batch, std::size_t frame,
    const std::filesystem::path& output, std::size_t sampleLimit)
{
    if (batch.Version != state.Version())
    {
        throw std::invalid_argument("只读质量审计不能使用过期批次");
    }
    std::filesystem::create_directories(output);
    const auto target = output / ("exchange-quality-" + std::to_string(frame) + ".json");
    // 一个文件代表一次完整观察，拒绝覆盖避免混淆不同状态的证据
    if (std::filesystem::exists(target))
    {
        throw std::runtime_error("拒绝覆盖交换质量证据");
    }
    CaptureWork work;
    auto started = Clock::now();
    const auto sourcePath = output / "exchange-source.json";
    // 调用方独占本轨迹目录，U16 只写一次，不在每个局部提案中复制整幅参考
    if (!std::filesystem::exists(sourcePath))
    {
        std::ofstream source(sourcePath);
        source.exceptions(std::ios::badbit | std::ios::failbit);
        source << "{\"width\":" << samples.Source().Width << ",\"height\":" << samples.Source().Height << ",\"values\":[";
        const auto& values = samples.Source().Values;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            source << (i == 0 ? "" : ",") << values[i];
        }
        source << "]}\n";
        work.SourceValues = values.size();
    }
    work.SourceSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    started = Clock::now();
    // 全根人口是额外诊断全扫，时间与访问量不能并入局部采集来隐藏
    Roots(state, samples, output / ("exchange-roots-" + std::to_string(frame) + ".csv"), work);
    work.RootsSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    started = Clock::now();
    std::vector<std::array<std::set<Slot>, 2>> supports;
    // 上限作用于整批完整记录；不因为后半批超限而保留貌似完整的前半批
    for (const auto& exchange : batch.Exchanges)
    {
        auto receiver = ClosedSamples(samples, exchange.Receiver, work);
        auto donor = exchange.HasDonor ? ClosedSamples(samples, exchange.Donor, work) : std::set<Slot>{};
        work.SampleRecords += receiver.size() + donor.size();
        supports.push_back({std::move(receiver), std::move(donor)});
        if (work.SampleRecords > sampleLimit)
        {
            break;
        }
    }
    const bool censored = work.SampleRecords > sampleLimit;
    // 删失时计数只描述已访问的前缀，approved 始终保留原完整批次分母
    const auto& config = state.Config();
    std::ofstream stream(target);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    // 17 位输出供 Fraction 恢复实际 double，不把十进制显示值当数学原值
    stream << std::setprecision(17) << "{\"schema\":1,\"status\":\"" << (censored ? "censored-samples" : "complete")
        << "\",\"frame\":" << frame << ",\"version\":" << state.Version() << ",\"approved\":" << batch.Exchanges.size()
        << ",\"denominator\":" << samples.Denominator() << ",\"faces\":" << state.FaceCount()
        << ",\"budget\":" << config.Budget << ",\"netFaces\":" << batch.NetFaceChange
        << ",\"terrainSize\":" << config.TerrainSize << ",\"heightScale\":" << config.HeightScale
        << ",\"width\":" << config.Width << ",\"height\":" << config.Height
        << ",\"zeroToOne\":" << config.UsesZeroToOneDepth
        << ",\"qualityPolicy\":\"" << (config.QualityPolicy == Algorithms::TransactionalQualityPolicy::PointwiseTarget ? "pointwise-target" : "legacy")
        << "\",\"qualityTargetPixels\":" << config.QualityTargetPixels
        << ",\"qualityHeightRatio\":" << config.QualityHeightRatio << ",\"matrix\":[";
    for (std::size_t i = 0; i < config.Matrix.size(); ++i)
    {
        stream << (i == 0 ? "" : ",");
        Number(stream, config.Matrix[i]);
    }
    stream << "],\"exchanges\":[";
    if (!censored)
    {
        // 按原批准顺序完整导出两侧，不能只记录当前有利或已知坏事务
        for (std::size_t i = 0; i < batch.Exchanges.size(); ++i)
        {
            const auto& exchange = batch.Exchanges[i];
            stream << (i == 0 ? "" : ",") << "{\"index\":" << i << ",\"kind\":" << static_cast<int>(exchange.Kind)
                << ",\"receiver\":";
            Side(stream, state, samples, exchange.Receiver, supports[i][0], work);
            stream << ",\"donor\":";
            if (exchange.HasDonor)
            {
                Side(stream, state, samples, exchange.Donor, supports[i][1], work);
            }
            else
            {
                // Free 与净零恢复都没有 donor；null 不等价于零成本捐赠提案
                stream << "null";
            }
            stream << '}';
        }
    }
    work.LocalSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    // 费用截至局部记录写入，最后的账本尾部和流关闭不算作几何认证时间
    stream << "],\"work\":{\"associations\":" << work.Associations << ",\"sampleRecords\":" << work.SampleRecords
        << ",\"visibleRecords\":" << work.Visible << ",\"rootContributions\":" << work.RootContributions
        << ",\"sourceValues\":" << work.SourceValues << ",\"sourceSeconds\":" << work.SourceSeconds
        << ",\"rootsSeconds\":" << work.RootsSeconds << ",\"localSeconds\":" << work.LocalSeconds << "}}\n";
}
}
