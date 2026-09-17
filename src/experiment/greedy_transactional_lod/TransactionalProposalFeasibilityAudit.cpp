#include "experiment/greedy_transactional_lod/TransactionalProposalFeasibilityAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalBoundaryRefinement.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"

#include <boost/property_tree/json_parser.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        // 来源不可读取就中止复核，不能把空字符串误解为无需核对来源
        throw std::runtime_error("无法读取冻结审计输入");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void String(std::ostream& out, const std::string& value)
{
    // 输入绑定包含JSON文本，逐字符转义，不能将它当原始外层JSON写入
    out << '"';
    for (const char ch : value)
    {
        if (ch == '"' || ch == '\\')
        {
            out << '\\';
        }
        if (ch == '\n')
        {
            out << "\\n";
        }
        else if (ch == '\r')
        {
            out << "\\r";
        }
        else
        {
            out << ch;
        }
    }
    out << '"';
}

void Geometry(std::ostream& out, const std::map<Identity, Point>& points,
    const std::vector<std::array<Identity, 3>>& faces)
{
    // 保留实际存储坐标与逻辑身份，离线端自行恢复曲面，不导出近似平面系数
    out << "{\"points\":[";
    bool first = true;
    for (const auto& [id, point] : points)
    {
        out << (first ? "" : ",") << '[' << id << ',' << point.U << ',' << point.V << ',' << point.Height << ']';
        first = false;
    }
    out << "],\"faces\":[";
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        out << (i == 0 ? "" : ",") << '[' << faces[i][0] << ',' << faces[i][1] << ',' << faces[i][2] << ']';
    }
    out << "]}";
}

void OldGeometry(std::ostream& out, const TransactionalState& state, const std::vector<Slot>& support)
{
    // 同一接口顶点只写一次；旧连接与新连接分开，不能假定插点前后天然共面
    std::map<Identity, Point> points;
    std::vector<std::array<Identity, 3>> faces;
    for (const auto slot : support)
    {
        const auto face = state.Face(slot).Vertices;
        faces.push_back(face);
        for (const auto id : face)
        {
            points.emplace(id, state.Vertex(id).Geometry);
        }
    }
    Geometry(out, points, faces);
}

std::string Source(const HeightSource& source)
{
    // U16原值是独立参考的依据，缓存的双线性高度只能用于核对舍入差异
    // 所有轨迹机会共用同一源文件，避免每项提案复制整幅高度图
    std::ostringstream out;
    out << "{\"width\":" << source.Width << ",\"height\":" << source.Height << ",\"values\":[";
    for (std::size_t i = 0; i < source.Values.size(); ++i)
    {
        out << (i == 0 ? "" : ",") << source.Values[i];
    }
    out << "]}\n";
    return out.str();
}

std::string View(const Configuration& config)
{
    // 深度约定、分辨率和质量目标都影响可行域，不能只用相机位置作绑定
    std::ostringstream out;
    out << std::setprecision(17) << "{\"terrainSize\":" << config.TerrainSize
        << ",\"heightScale\":" << config.HeightScale << ",\"width\":" << config.Width
        << ",\"height\":" << config.Height << ",\"zeroToOne\":" << config.UsesZeroToOneDepth
        << ",\"qualityTargetPixels\":" << config.QualityTargetPixels
        << ",\"qualityHeightRatio\":" << config.QualityHeightRatio << ",\"matrix\":[";
    for (std::size_t i = 0; i < config.Matrix.size(); ++i)
    {
        out << (i == 0 ? "" : ",") << config.Matrix[i];
    }
    out << "]}";
    return out.str();
}

std::string Binding(const TransactionalState& state, const TransactionalSamples& samples,
    const Proposal& proposal, std::size_t frame, std::size_t ordinal)
{
    // 文本使用binary64往返精度；外部见证必须匹配未拟合输入，而非事后结果
    // 大体积源数据另作完整内容比较，物理面槽位不进入持久身份
    std::ostringstream out;
    out << std::setprecision(17) << "{\"frame\":" << frame << ",\"version\":" << state.Version()
        << ",\"denominator\":" << samples.Denominator() << ",\"root\":" << state.Face(proposal.Root).Id
        << ",\"ordinal\":" << ordinal << ",\"kind\":\"" << proposal.Kind << "\",\"newVertex\":"
        << proposal.NewVertex << ",\"config\":" << View(state.Config()) << ",\"old\":";
    OldGeometry(out, state, proposal.Support);
    out << ",\"new\":";
    Geometry(out, proposal.Points, proposal.Faces);
    out << '}';
    return out.str();
}

std::uint64_t Fingerprint(const std::string& text)
{
    // 仅做重复输入的定位摘要；见证复核仍比较完整原文和完整源数据
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::set<Slot> Closed(const TransactionalSamples& samples, const Proposal& proposal)
{
    // 高度损伤约束涵盖不可见点，共享边也不能因owner在另一面而遗漏
    std::set<Slot> result;
    for (const auto slot : proposal.Support)
    {
        const auto& values = samples.FaceSamples(slot);
        result.insert(values.begin(), values.end());
    }
    return result;
}
}

TransactionalProposalFeasibilityAudit::TransactionalProposalFeasibilityAudit(
    const std::filesystem::path& specification, std::size_t frameCount)
{
    boost::property_tree::ptree tree;
    boost::property_tree::read_json(specification.string(), tree);
    _costAudit = tree.get<bool>("costAudit", false);
    if (tree.get<std::string>("protocol") != "qpc04g-v1")
    {
        // 旧探针的身份和高度语义不同，不接受无版本或隐式兼容输入
        throw std::invalid_argument("提案审计协议不匹配");
    }
    for (const auto& item : tree.get_child("requests"))
    {
        // 机会与逻辑根都由文件提前指定，不能在看到可行结果后换一个根
        const auto frame = item.second.get<std::size_t>("frame");
        if (frame >= frameCount || _roots.contains(frame))
        {
            throw std::invalid_argument("审计机会越界或重复");
        }
        auto& roots = _roots[frame];
        // 保存用户冻结的根顺序，避免有序集合按负身份重新安排诊断顺序
        for (const auto& value : item.second.get_child("roots"))
        {
            const auto root = value.second.get_value<Identity>();
            if (std::find(roots.begin(), roots.end(), root) != roots.end())
            {
                throw std::invalid_argument("审计根重复");
            }
            roots.push_back(root);
            _trackedRoots.insert(root);
        }
        if (roots.empty())
        {
            throw std::invalid_argument("审计请求没有根");
        }
    }
    if (_roots.empty() || _roots.size() > 2 || _trackedRoots.size() > 7)
    {
        throw std::invalid_argument("审计范围超过冻结上限");
    }
    if (const auto path = tree.get_optional<std::string>("witnessFile"))
    {
        // 只允许每个固定目录项一个高度，防止复核阶段暗中变成新的搜索过程
        boost::property_tree::ptree witnesses;
        boost::property_tree::read_json(*path, witnesses);
        _expectedSource = Read(witnesses.get<std::string>("sourceFile"));
        std::set<std::tuple<std::size_t, Identity, std::size_t>> unique;
        for (const auto& item : witnesses.get_child("entries"))
        {
            const auto& row = item.second;
            Witness witness{row.get<std::size_t>("frame"), row.get<std::size_t>("ordinal"),
                row.get<Identity>("root"), row.get<double>("height"), row.get<std::string>("binding")};
            if (!_roots.contains(witness.Frame) || witness.Ordinal >= 8 || !std::isfinite(witness.Height) ||
                std::find(_roots.at(witness.Frame).begin(), _roots.at(witness.Frame).end(), witness.Root) ==
                    _roots.at(witness.Frame).end() ||
                !unique.emplace(witness.Frame, witness.Root, witness.Ordinal).second)
            {
                // 非有限高度、重复见证和目录之外的请求都不是可判定的质量输入
                throw std::invalid_argument("高度见证超出冻结目录");
            }
            _witnesses.push_back(std::move(witness));
        }
    }
}

void TransactionalProposalFeasibilityAudit::VerifyWitnesses(std::ostream& out,
    const TransactionalState& state, const TransactionalSamples& samples, const Proposal& initial,
    std::size_t frame, std::size_t ordinal, const std::string& binding) const
{
    // 该方法仅认证外部给定值，没有Apply或生产队列的可写引用
    const auto root = state.Face(initial.Root).Id;
    for (const auto& witness : _witnesses)
    {
        if (witness.Frame != frame || witness.Root != root || witness.Ordinal != ordinal)
        {
            continue;
        }
        if (witness.Binding != binding || initial.Kind == 'B' || !initial.Reason.empty() ||
            witness.Height < -state.Config().HeightScale || witness.Height > 2 * state.Config().HeightScale)
        {
            // B的源高中点是构造规则，即使数据结构有Free字段也不能自由改高
            throw std::invalid_argument("见证改变来源、固定提案或高度范围");
        }
        auto candidate = initial;
        // 从未拟合描述重新开始，避免复用旧Fit高度、缓存误差或已有证书
        candidate.Points.at(candidate.NewVertex).Height = witness.Height;
        for (const auto& face : candidate.Faces)
        {
            // 质量认证不代替结构认证，固定目录的每个三角形先满足原形状条件
            if (!TransactionalPredicates::Shape(candidate.Points.at(face[0]), candidate.Points.at(face[1]),
                candidate.Points.at(face[2])))
            {
                throw std::invalid_argument("见证的固定目录形状不合法");
            }
        }
        WorkLedger check;
        // 私有账本与原Plan隔离，诊断访问不混入生产工作量或停止条件
        check.VisitLimit = 2000000;
        check.Deadline = Clock::now() + std::chrono::seconds(20);
        const auto reason = TransactionalPointwiseQuality::Certify(state, samples, candidate, true, check);
        std::vector<double> costs;
        if (_costAudit)
        {
            // 上面的首次认证作为预热；短重复只用于同提案定位，不充当独立进程
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                WorkLedger measured;
                measured.VisitLimit = 2000000;
                measured.Deadline = Clock::now() + std::chrono::seconds(1);
                const auto started = Clock::now();
                const auto repeated = TransactionalPointwiseQuality::Certify(state, samples, candidate, true, measured);
                candidate.QualityProof.reset();
                costs.push_back(Seconds(started));
                if (repeated != reason || measured.SampleTouches != check.SampleTouches)
                {
                    throw std::runtime_error("相同提案重复认证改变理由或支持访问");
                }
            }
        }
        // 旧最大值门槛独立核对，不把绕过Fit误称为旧完整流水线接受
        const auto targetReason = TransactionalCertification::SetProgressTarget(state, samples, candidate, check);
        const bool legacy = targetReason.empty() &&
            TransactionalCertification::Measure(state, samples, candidate, check) &&
            TransactionalCertification::Accepts(state, samples, candidate, candidate.TargetMicropixels, check);
        out << "{\"height\":" << witness.Height << ",\"pointwiseReason\":\"" << reason
            << "\",\"legacyAccepts\":" << legacy << ",\"touches\":" << check.SampleTouches;
        if (_costAudit)
        {
            out << ",\"certifySeconds\":[";
            for (std::size_t i = 0; i < costs.size(); ++i)
            {
                out << (i == 0 ? "" : ",") << costs[i];
            }
            out << ']';
        }
        out << '}';
    }
}

void TransactionalProposalFeasibilityAudit::CaptureProposal(const TransactionalState& state,
    const TransactionalSamples& samples, const Proposal& initial, std::size_t frame,
    std::size_t ordinal, bool inPrefix, const std::filesystem::path& output)
{
    // 单项输入、旧路径对照及外部见证写入同一记录，避免跨目录项拼接证据
    const auto root = state.Face(initial.Root).Id;
    const auto file = output / ("proposal-" + std::to_string(frame) + "-" +
        std::to_string(root) + "-" + std::to_string(ordinal) + ".json");
    if (std::filesystem::exists(file))
    {
        // 同一文件不能混入第二次运行的样本或见证；重放使用独立输出目录
        throw std::runtime_error("拒绝覆盖提案审计证据");
    }
    const auto binding = Binding(state, samples, initial, frame, ordinal);
    const auto closed = Closed(samples, initial);
    // 配额截断只能产生censored；不导出部分样本却声称完整支持可行
    const bool censored = closed.size() > 200000 || _sampleRecords + closed.size() > 2000000;
    _sampleRecords += censored ? 0 : closed.size();
    auto proposal = initial;
    // 旧Fit保持原调用顺序，仅对私有副本运行；区间未生成时保留available=false
    WorkLedger work;
    work.VisitLimit = 2000000;
    work.Deadline = Clock::now() + std::chrono::seconds(20);
    SingleHeightFitInterval interval;
    std::string oldReason = "censored", qualityReason = "not_run";
    const auto fitStarted = Clock::now();
    if (!censored)
    {
        // B走固定源高认证，E/F/H才进入单新点拟合，不按Free字段猜测语义
        oldReason = proposal.Kind == 'B' ?
            TransactionalBoundaryRefinement::Certify(state, samples, proposal, work) :
            TransactionalCertification::Fit(state, samples, proposal, work, &interval);
        if (oldReason == "certified")
        {
            // 旧最大值门槛与新逐点契约是两层条件，分别记录拒绝位置
            qualityReason = TransactionalPointwiseQuality::Certify(state, samples, proposal, true, work);
        }
    }
    const auto fitSeconds = Seconds(fitStarted);
    // 对照费用包含旧认证后的逐点检查，不能把它标成纯拟合求解时间
    std::ofstream out(file, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(17) << "{\"schema\":1,\"status\":\""
        << (censored ? "censored" : "complete") << "\",\"input\":" << binding << ",\"binding\":";
    String(out, binding);
    out << ",\"inPrefix\":" << inPrefix
        << ",\"constructionReason\":\"" << initial.Reason << "\",\"oldReason\":\"" << oldReason
        << "\",\"pointwiseReason\":\"" << qualityReason << "\",\"targetMicropixels\":"
        << proposal.TargetMicropixels << ",\"fitHeight\":";
    if (proposal.Points.contains(proposal.NewVertex))
    {
        out << proposal.Points.at(proposal.NewVertex).Height;
    }
    else
    {
        // 构造可能在分辨率检查时提前失败，不能假设新点已经存在
        out << "null";
    }
    out << ",\"interval\":{\"available\":" << interval.Available << ",\"initial\":" << interval.InitialHeight
        << ",\"lower\":" << interval.LowerDelta << ",\"upper\":" << interval.UpperDelta << "},\"samples\":[";
    bool first = true;
    if (!censored)
    {
        // 整数样本坐标供离线恢复精确Q；附带缓存值仅审计，不充当证明前提
        for (const auto sid : closed)
        {
            const auto xy = samples.Decode(sid);
            const auto value = samples.Value(sid);
            out << (first ? "" : ",") << '[' << sid << ',' << xy[0] << ',' << xy[1] << ','
                << value.Visible << ',' << value.MeshHeight << ',' << value.ReferenceHeight << ','
                << value.ErrorSquared << ']';
            first = false;
        }
    }
    out << "],\"oldWork\":{\"seconds\":" << fitSeconds << ",\"touches\":" << work.SampleTouches
        << ",\"constraints\":" << work.Constraints << ",\"exactChecks\":" << work.ExactChecks << "}";
    // 见证只作用于新建私有提案；完整几何绑定不等则在调用认证前拒绝
    out << ",\"verifiedWitnesses\":[";
    VerifyWitnesses(out, state, samples, initial, frame, ordinal, binding);
    out << "]}\n";
}

void TransactionalProposalFeasibilityAudit::Observe(const TransactionalState& state,
    const TransactionalSamples& samples, std::size_t frame, const std::filesystem::path& output)
{
    // 该诊断只覆盖冻结旧点的逐点政策，不把别的生产自由度投影成一维问题
    const auto started = Clock::now();
    if (!state.Config().PreserveSurvivingHeights ||
        state.Config().ReceiverHeightPolicy != Algorithms::TransactionalReceiverHeightPolicy::LegacyFit ||
        state.Config().QualityPolicy != Algorithms::TransactionalQualityPolicy::PointwiseTarget)
    {
        throw std::invalid_argument("旧Fit可行域审计要求冻结旧点、旧拟合及逐点目标政策");
    }
    std::filesystem::create_directories(output);
    const auto sourcePath = output / "feasibility-source.json";
    if (!std::filesystem::exists(sourcePath))
    {
        // 只在首次观察绑定源文件；后续快照沿用同一不可变HeightSource
        const auto text = Source(samples.Source());
        if (!_expectedSource.empty() && text != _expectedSource)
        {
            throw std::invalid_argument("见证源数据与当前状态不一致");
        }
        std::ofstream sourceOutput(sourcePath, std::ios::binary);
        sourceOutput.exceptions(std::ios::badbit | std::ios::failbit);
        sourceOutput << text;
    }
    std::map<Identity, Slot> found;
    // 只读诊断允许一次全根身份定位；扫描数量单列，不冒充局部生产工作
    for (const auto slot : state.ActiveFaces())
    {
        if (_trackedRoots.contains(state.Face(slot).Id))
        {
            found.emplace(state.Face(slot).Id, slot);
        }
    }
    const auto view = View(state.Config());
    std::ofstream repeats(output / "feasibility-inputs.csv", std::ios::app);
    if (frame == 0)
    {
        repeats << "frame,root,active,geometryFingerprint,viewFingerprint,prioritySquared\n";
    }
    const auto prefix = samples.Prefix(state.Config().PrefixLimit);
    for (const auto root : _trackedRoots)
    {
        if (!found.contains(root))
        {
            // 根死亡是输入生命周期中断，不改用同槽位的新面延续重复统计
            repeats << frame << ',' << root << ",0,0," << Fingerprint(view) << ",0\n";
            continue;
        }
        const auto slot = found.at(root);
        std::set<Slot> neighborhood;
        // H目录依赖顶点邻域，只有根三点不变不足以证明失败输入可复用
        for (const auto id : state.Face(slot).Vertices)
        {
            const auto& incident = state.Vertex(id).Incident;
            neighborhood.insert(incident.begin(), incident.end());
        }
        std::ostringstream geometry;
        geometry << std::setprecision(17);
        OldGeometry(geometry, state, {neighborhood.begin(), neighborhood.end()});
        repeats << std::setprecision(17) << frame << ',' << root << ",1," << Fingerprint(geometry.str())
            << ',' << Fingerprint(view) << ',' << samples.PrioritySquared(slot) << '\n';
    }
    if (_roots.contains(frame))
    {
        for (const auto root : _roots.at(frame))
        {
            if (!found.contains(root))
            {
                throw std::runtime_error("冻结根在指定机会不存在");
            }
            const auto slot = found.at(root);
            ReceiverCursor cursor(state, samples, slot);
            // 枚举实际冻结目录，不因前一个项目成功而漏报后续项目
            std::size_t ordinal = 0;
            while (auto initial = cursor.Next())
            {
                CaptureProposal(state, samples, *initial, frame, ordinal,
                    std::find(prefix.begin(), prefix.end(), slot) != prefix.end(), output);
                ++ordinal;
            }
            // 一个不存在的目录序号也必须拒绝，不能静默漏掉外部复核请求
            for (const auto& witness : _witnesses)
            {
                if (witness.Frame == frame && witness.Root == root && witness.Ordinal >= ordinal)
                {
                    throw std::invalid_argument("见证目录项未由当前快照生成");
                }
            }
        }
    }
    std::ofstream timing(output / "feasibility-capture.csv", std::ios::app);
    // 该时间包含只读身份扫描和输出，不能从完整事务计时中悄悄扣除
    if (frame == 0)
    {
        timing << "frame,seconds,identityScans,cumulativeSampleRecords\n";
    }
    timing << std::setprecision(17) << frame << ',' << Seconds(started) << ',' << state.FaceCount()
        << ',' << _sampleRecords << '\n';
}
}
