#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualitySampleEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualityFaceEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalHeightRejection.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "profiling/CpuProfiling.h"

#include <set>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
using namespace QualityEvaluation;
using QualityFace = std::array<Point, 3>;

/// <summary>
/// 拥有冻结提案与两种曲面的支持，发布时不再依赖可变的目录游标
/// </summary>
struct ProposalQualityCertificate
{
    // 相同Version不能区分两个独立状态，证书也绑定存活的状态实例
    const TransactionalState *Owner{};
    std::uint64_t Version{};
    Configuration Config;
    char Kind{};
    Slot Root{};
    Identity Center{}, NewVertex{};
    std::vector<Slot> Support;
    std::vector<Identity> Free;
    std::map<Identity, Point> Points;
    std::vector<std::array<Identity, 3>> Faces;
    std::array<std::vector<Slot>, 2> Samples;
    // 两个域分别是核心binary64曲面和实际float位置曲面，不混合势函数
    bool Receiver{};
};

namespace
{
/// <summary>
/// 提案内统计在任何普通拒绝出口汇总，失败的精确计算也必须计费
/// </summary>
struct QualityWork
{
    WorkLedger &Ledger;
    std::uint64_t Samples{}, Exact{}, Filters{}, Bytes{}, EarlyHeightRejects{}, ScreenEvaluations{};
    std::uint64_t HintOutside{}, HintChecks{}, HintRejected{}, DomainLoops{};
    QualityEvidenceWork Evidence{};
    QualityFaceEvidenceWork FaceEvidence{};
    const std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
    ~QualityWork()
    {
        // 工作线程局部汇总后由现有执行器合并，避免计数成为共享写热点
        Ledger.Reasons["quality_samples"] += Samples;
        Ledger.Reasons["quality_exact_samples"] += Exact;
        Ledger.Reasons["quality_filter_samples"] += Filters;
        Ledger.Reasons["quality_evidence_bytes"] += Bytes;
        Ledger.Reasons["quality_early_height_rejects"] += EarlyHeightRejects;
        Ledger.Reasons["quality_screen_evaluations"] += ScreenEvaluations;
        Ledger.Reasons["quality_hint_outside"] += HintOutside;
        Ledger.Reasons["quality_hint_checks"] += HintChecks;
        Ledger.Reasons["quality_hint_rejected"] += HintRejected;
        Ledger.Reasons["quality_full_domain_loops"] += DomainLoops;
        Ledger.Reasons["quality_contexts"] += Evidence.Contexts;
        Ledger.Reasons["quality_reference_bounds_builds"] += Evidence.BoundsReferences;
        Ledger.Reasons["quality_reference_exact_builds"] += Evidence.ExactReferences;
        Ledger.Reasons["quality_clip_bounds_builds"] += Evidence.BoundsClips;
        Ledger.Reasons["quality_clip_exact_builds"] += Evidence.ExactClips;
        Ledger.Reasons["quality_cover_exact_requests"] += Evidence.ExactCoverRequests;
        Ledger.Reasons["quality_visibility_exact_requests"] += Evidence.ExactVisibilityRequests;
        Ledger.Reasons["quality_face_records"] += FaceEvidence.Records;
        Ledger.Reasons["quality_face_bounds_builds"] += FaceEvidence.BoundsBuilds;
        Ledger.Reasons["quality_face_exact_builds"] += FaceEvidence.ExactBuilds;
        Ledger.Reasons["quality_face_bounds_sides"] += FaceEvidence.BoundsSides;
        Ledger.Reasons["quality_face_exact_sides"] += FaceEvidence.ExactSides;
        Ledger.Reasons["quality_face_bounds_heights"] += FaceEvidence.BoundsHeights;
        Ledger.Reasons["quality_face_exact_heights"] += FaceEvidence.ExactHeights;
        Ledger.Seconds["pointwise_quality"] +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
#if defined(TRACY_ENABLE)
        // 每个认证只导出一次聚合工作量，避免在样本循环中产生诊断事件
        ROAM_CPU_ZONE("gtp.face_evidence");
        const auto counts = std::to_string(FaceEvidence.Records) + "/" +
            std::to_string(FaceEvidence.BoundsBuilds) + "/" + std::to_string(FaceEvidence.ExactBuilds) + "/" +
            std::to_string(FaceEvidence.BoundsSides) + "/" + std::to_string(FaceEvidence.ExactSides) + "/" +
            std::to_string(FaceEvidence.BoundsHeights) + "/" + std::to_string(FaceEvidence.ExactHeights);
        ROAM_CPU_TEXT(counts.data(), counts.size());
#endif
    }
};

std::string CheckSample(const TransactionalState &state, const TransactionalSamples &samples, Slot sid,
                        TransactionalQualitySampleEvidence& evidence,
                        TransactionalQualityFaceEvidence& old, TransactionalQualityFaceEvidence& next,
                        QualityWork &work, Integer &gainLow,
                        Integer &gainHigh, HeightRejectionContext* rejection)
{
    ++work.Samples;
    work.Ledger.Touch();
    const auto &config = state.Config();
    // 先校验参考可见性的人口，再判断被测几何；几何不能隐藏自身的坏样本
    const auto& ref = evidence.ReferenceBounds();
    if (!evidence.VisibilityAgrees())
    {
        return "pointwise_visibility_unknown";
    }
    const auto& uv = evidence.CoordinateBounds();
    const auto h0 = old.HeightBounds(uv);
    const auto h1 = next.HeightBounds(uv);
    // 高度残差跨相机保持含义，离屏点也必须走这个分支
    const auto v0 = h0 - ref[2], v1 = h1 - ref[2];
    const auto u0 = v0 * v0, u1 = v1 * v1;
    const Interval target = Interval(config.HeightScale) * Interval(config.QualityHeightRatio);
    const Interval heightCap = target * target;
    if (rejection && rejection->HeightFirst && TransactionalHeightRejection::ProvesDamage(u0, u1, heightCap))
    {
        ++work.Filters;
        ++work.EarlyHeightRejects;
        rejection->FailureSample = sid;
        rejection->IntervalFailure = true;
        return "pointwise_height_damage";
    }
    const Interval screenCap = Interval(config.QualityTargetPixels) * Interval(config.QualityTargetPixels);
    const bool visible = samples.Projection(sid).Visible;
    work.ScreenEvaluations += visible ? 2 : 0;
    // 不可见项不进入屏幕势函数，但上面的高度分支不会被跳过
    const auto a0 = visible ? ScreenPrepared(config, ref, evidence.ClipBounds(), h0) : std::optional<Interval>{Interval(0)};
    const auto a1 = visible ? ScreenPrepared(config, ref, evidence.ClipBounds(), h1) : std::optional<Interval>{Interval(0)};
    // 拒绝需要整个新区间都超过允许上界，区间重叠不能当作真实损伤
    ++work.Filters;
    if (Finite(u0) && Finite(u1) && u1.Low > std::max(u0.High, heightCap.High))
    {
        if (rejection)
        {
            rejection->FailureSample = sid;
            rejection->IntervalFailure = true;
        }
        return "pointwise_height_damage";
    }
    if (visible && a0 && a1 && Finite(*a0) && Finite(*a1) && a1->Low > std::max(a0->High, screenCap.High))
    {
        return "pointwise_screen_damage";
    }
    const bool heightSafe = Finite(u0) && Finite(u1) && u1.High <= std::max(u0.Low, heightCap.Low);
    const bool screenSafe =
        !visible || (a0 && a1 && Finite(*a0) && Finite(*a1) && a1->High <= std::max(a0->Low, screenCap.Low));
    // 两种损伤都得到充分证据后才累计快速进展，不能只证明最大值安全
    if (heightSafe && screenSafe)
    {
        if (visible)
        {
            // 正部的上下界分别截断；目标以下的损伤不会冒充负超标进展
            const Interval before{std::max(0.0, Down(a0->Low - screenCap.High)),
                                  std::max(0.0, Up(a0->High - screenCap.Low))};
            const Interval after{std::max(0.0, Down(a1->Low - screenCap.High)),
                                 std::max(0.0, Up(a1->High - screenCap.Low))};
            const auto delta = before - after;
            AddBinaryBounds(delta.Low, delta.High, gainLow, gainHigh);
        }
        return {};
    }
    // 临界相等使用精确值；这也覆盖区间不能判断投影定义域的情况
    ++work.Exact;
    const auto& exactRef = evidence.ExactReference();
    const auto& exactUV = evidence.ExactCoordinate();
    const R oldHeight = old.ExactHeight(exactUV);
    const R newHeight = next.ExactHeight(exactUV);
    const R oldResidual = oldHeight - exactRef[2], newResidual = newHeight - exactRef[2];
    RecordBits(oldResidual, work.Ledger);
    RecordBits(newResidual, work.Ledger);
    const R h = R(config.HeightScale) * R(config.QualityHeightRatio);
    // 等号合法，所有输入都解释为实际二进制值，不添加经验epsilon
    if (newResidual * newResidual > std::max(R(oldResidual * oldResidual), R(h * h)))
    {
        if (rejection)
        {
            rejection->FailureSample = sid;
        }
        return "pointwise_height_damage";
    }
    if (visible)
    {
        work.ScreenEvaluations += 2;
        const auto before = ScreenPrepared(config, exactRef, evidence.ExactClip(), oldHeight);
        const auto after = ScreenPrepared(config, exactRef, evidence.ExactClip(), newHeight);
        if (!before || !after)
        {
            return "pointwise_projection_unknown";
        }
        const R cap = R(config.QualityTargetPixels) * R(config.QualityTargetPixels);
        if (*after > std::max(*before, cap))
        {
            return "pointwise_screen_damage";
        }
        const R delta = std::max(R(0), R(*before - cap)) - std::max(R(0), R(*after - cap));
        // 独立记录参与比较的有理位长，不把每次精确调用视为恒定CPU成本
        RecordBits(delta, work.Ledger);
        AddBounds(delta, delta, gainLow, gainHigh);
    }
    return {};
}
} // namespace

bool TransactionalPointwiseQuality::Enabled(const Configuration &config)
{
    return config.QualityPolicy == TransactionalQualityPolicy::PointwiseTarget;
}

void TransactionalPointwiseQuality::Validate(const Configuration &config)
{
    // 校验同时用于直接构造核心和平台初始化，不能只在JSON入口保护
    if (config.QualityPolicy != TransactionalQualityPolicy::Legacy && !Enabled(config))
    {
        throw std::invalid_argument("未知逐点质量政策");
    }
    if (config.ReceiverHeightPolicy != TransactionalReceiverHeightPolicy::LegacyFit &&
        config.ReceiverHeightPolicy != TransactionalReceiverHeightPolicy::SourceHeight)
    {
        throw std::invalid_argument("未知接收新点高度政策");
    }
    if (config.ReceiverHeightPolicy == TransactionalReceiverHeightPolicy::SourceHeight && !Enabled(config))
    {
        throw std::invalid_argument("源高度接收提案要求逐点质量政策");
    }
    if (!std::isfinite(config.QualityTargetPixels) || config.QualityTargetPixels <= 0 ||
        !std::isfinite(config.QualityHeightRatio) || config.QualityHeightRatio <= 0)
    {
        throw std::invalid_argument("质量目标必须有限且为正");
    }
    if (Enabled(config) && (!config.PreserveSurvivingHeights || config.HeightGuard ||
                            config.ReceiverOrder != TransactionalReceiverOrder::ErrorFirst))
    {
        throw std::invalid_argument("逐点政策要求固定旧点、误差优先并关闭旧高度保护");
    }
}

std::string TransactionalPointwiseQuality::Certify(const TransactionalState &state, const TransactionalSamples &samples,
                                                   Proposal &proposal, bool receiver, WorkLedger &ledger,
                                                   HeightRejectionContext* rejection)
{
    if (rejection)
    {
        rejection->FailureSample = InvalidSlot;
        rejection->IntervalFailure = false;
        rejection->OriginalReason.clear();
    }
    if (!Enabled(state.Config()))
    {
        return "certified";
    }
    ROAM_CPU_ZONE("gtp.pointwise.certify");
    // 拒绝或异常都不能残留此前提案的成功证书
    proposal.QualityProof.reset();
    // 消费通过旧认证或源高结构准备的合法目录项，不检查任意三角化是否合法
    QualityWork work{ledger};
    auto proof = std::make_shared<ProposalQualityCertificate>();
    // 发布前逐字段比对用于防止缓存证书被复制到另一份拟合结果
    proof->Owner = &state;
    proof->Version = state.Version();
    proof->Config = state.Config();
    proof->Kind = proposal.Kind;
    proof->Root = proposal.Root;
    proof->Center = proposal.Center;
    proof->NewVertex = proposal.NewVertex;
    proof->Support = proposal.Support;
    proof->Free = proposal.Free;
    proof->Points = proposal.Points;
    proof->Faces = proposal.Faces;
    proof->Receiver = receiver;
    // 只保存有界几何与本提案支持，避免将完整Q或生产状态复制进证书
    work.Bytes += sizeof(*proof) + proof->Points.size() * sizeof(std::pair<Identity, Point>) +
                  proof->Faces.size() * sizeof(std::array<Identity, 3>);
    for (std::size_t domain = 0; domain < 2; ++domain)
    {
        // 两域分别成功才接受；这比仅保护私有拟合曲面更保守
        const bool output = domain == 1;
        const auto old = Faces(state, proposal, output, false), next = Faces(state, proposal, output, true);
        if (old.empty() || next.empty() || !SameInterface(state, proposal, output, old, next))
        {
            return output ? "pointwise_output_interface" : "pointwise_interface";
        }
        std::vector<Slot> candidates;
        if (!output)
        {
            // 核心域复用已维护的闭面关联，owner唯一不等于贡献唯一
            for (auto slot : proposal.Support)
            {
                const auto &closed = samples.FaceSamples(slot);
                candidates.insert(candidates.end(), closed.begin(), closed.end());
            }
        }
        else
        {
            // float边界可能稍移；不能直接沿用binary64的FaceSamples作为完整支持
            double minU = 1, minV = 1, maxU = 0, maxV = 0;
            for (const auto &f : old)
            {
                for (const auto &p : f)
                {
                    const double u = p.U / state.Config().TerrainSize + .5;
                    const double v = p.V / state.Config().TerrainSize + .5;
                    minU = std::min(minU, u);
                    minV = std::min(minV, v);
                    maxU = std::max(maxU, u);
                    maxV = std::max(maxV, v);
                }
            }
            candidates = samples.BoxCandidates(minU, minV, maxU, maxV, ledger);
        }
        // 闭面贡献会重复，去重发生在质量求值前而不是进展求和后
        std::sort(candidates.begin(), candidates.end());
        // 一个Q在多个旧面上出现时只支付一次质量比较，不能累加虚假进展
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        // 每个表示域独立累计，内部改善不能补贴公开曲面的无进展
        Integer low = 0, high = 0;
        // 面证据只活到当前域结束；不同提案高度和float输出不得复用旧系数
        const auto prepare = [&](const auto& faces) {
            std::vector<TransactionalQualityFaceEvidence> result;
            result.reserve(faces.size());
            for (const auto& face : faces)
            {
                result.emplace_back(face, &work.FaceEvidence);
            }
            work.Bytes += result.capacity() * sizeof(TransactionalQualityFaceEvidence);
            return result;
        };
        auto oldEvidence = prepare(old);
        auto nextEvidence = prepare(next);
        // 只有源高接收的核心域参与消融，公开域和其他生成器仍按原序检查
        auto* activeRejection = !output && receiver &&
            state.Config().ReceiverHeightPolicy == TransactionalReceiverHeightPolicy::SourceHeight &&
            (proposal.Kind == 'E' || proposal.Kind == 'F' || proposal.Kind == 'H') ? rejection : nullptr;
        if (activeRejection && activeRejection->HintSample != InvalidSlot)
        {
            ROAM_CPU_ZONE("gtp.height_hint_check");
            const auto started = std::chrono::steady_clock::now();
            const auto sid = activeRejection->HintSample;
            bool reject = false;
            if (!std::binary_search(candidates.begin(), candidates.end(), sid))
            {
                ++work.HintOutside;
            }
            else
            {
                ++work.HintChecks;
                ++work.Samples;
                ledger.Touch();
                TransactionalQualitySampleEvidence evidence(state, samples, sid, false, &work.Evidence);
                reject = TransactionalHeightRejection::Recheck(state.Config(), evidence, oldEvidence, nextEvidence);
            }
            ledger.Seconds["height_hint_check"] +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if (reject)
            {
                ++work.HintRejected;
                activeRejection->FailureSample = sid;
                activeRejection->IntervalFailure = true;
                if (activeRejection->Audit)
                {
                    // 影子重放只在诊断遍执行，不合入生产工作量或发布结果
                    const auto auditStarted = std::chrono::steady_clock::now();
                    auto original = proposal;
                    WorkLedger audit;
                    audit.Deadline = ledger.Deadline;
                    audit.VisitLimit = ledger.VisitLimit;
                    activeRejection->OriginalReason = Certify(state, samples, original, receiver, audit);
                    if (activeRejection->OriginalReason == "certified")
                    {
                        throw std::runtime_error("高度提示错误拒绝了原认证可接受的提案");
                    }
                    ++ledger.Reasons["height_hint_shadow_" + activeRejection->OriginalReason];
                    ledger.Seconds["height_hint_shadow"] +=
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - auditStarted).count();
                }
                return "pointwise_height_damage";
            }
        }
        ++work.DomainLoops;
        for (auto sid : candidates)
        {
            TransactionalQualitySampleEvidence evidence(state, samples, sid, output, &work.Evidence);
            auto* before = evidence.Cover(oldEvidence);
            auto* after = evidence.Cover(nextEvidence);
            // 包围盒外扩产生的域外点跳过，旧新覆盖不一致则不能接受
            if (!before && !after)
            {
                continue;
            }
            if (!before || !after)
            {
                return "pointwise_coverage_unknown";
            }
            proof->Samples[domain].push_back(sid);
            const auto reason = CheckSample(state, samples, sid, evidence, *before, *after, work, low, high, activeRejection);
            if (!reason.empty())
            {
                return reason;
            }
        }
        work.Bytes += candidates.capacity() * sizeof(Slot) + proof->Samples[domain].capacity() * sizeof(Slot);
        // donor的非负进展已由逐点条件保证；receiver另需严格正下界
        // 跨零只说明当前数值证据不足，不能改成一个微小正数
        if (receiver && low <= 0)
        {
            return high <= 0 ? "pointwise_no_progress" : "pointwise_progress_unknown";
        }
    }
    proposal.QualityProof = std::move(proof);
    // 只有走完整个支持后才发布成功标志，不能提前把部分检查当作证书
    ++ledger.Reasons[receiver ? "pointwise_receiver_certified" : "pointwise_donor_certified"];
    return "certified";
}

void TransactionalPointwiseQuality::ValidateBatch(const TransactionalState &state, const TransactionalSamples &samples,
                                                  const CertifiedBatch &batch, WorkLedger &work)
{
    if (!Enabled(state.Config()))
    {
        return;
    }
    ROAM_CPU_ZONE("gtp.pointwise.validate_batch");
    const auto started = std::chrono::steady_clock::now();
    // 共享索引是本次发布的临时对象，不进入跨帧缓存或候选队列
    std::array<std::map<Slot, std::vector<const Proposal *>>, 2> shared;
    const auto visit = [&](const Proposal &p, bool receiver) {
        // 比较有限几何记录而非哈希，避免把碰撞概率引入正确性契约
        const auto &proof = p.QualityProof;
        const auto &config = state.Config();
        if (!proof || proof->Owner != &state || proof->Version != state.Version() || proof->Receiver != receiver ||
            proof->Config.Matrix != config.Matrix || proof->Config.Width != config.Width ||
            proof->Config.Height != config.Height || proof->Config.UsesZeroToOneDepth != config.UsesZeroToOneDepth ||
            proof->Config.QualityTargetPixels != config.QualityTargetPixels ||
            proof->Config.ReceiverHeightPolicy != config.ReceiverHeightPolicy ||
            proof->Config.QualityHeightRatio != config.QualityHeightRatio || proof->Kind != p.Kind ||
            proof->Root != p.Root || proof->Center != p.Center || proof->NewVertex != p.NewVertex ||
            proof->Support != p.Support || proof->Free != p.Free || proof->Faces != p.Faces ||
            proof->Points != p.Points)
        {
            throw std::runtime_error("逐点证书与发布提案或代次不一致");
        }
        for (std::size_t domain = 0; domain < 2; ++domain)
        {
            for (auto sid : proof->Samples[domain])
            {
                shared[domain][sid].push_back(&p);
            }
        }
    };
    for (const auto &exchange : batch.Exchanges)
    {
        // 整个交换两侧不可拆分；任何过期证书都会在生产状态修改前失败
        visit(exchange.Receiver, true);
        if (exchange.HasDonor)
        {
            visit(exchange.Donor, false);
        }
    }
    for (std::size_t domain = 0; domain < 2; ++domain)
    {
        // 唯一贡献已在提案内认证，批次只复核交集，不重新扫描全参考Q
        for (const auto &[sid, proposals] : shared[domain])
        {
            if (proposals.size() < 2)
            {
                continue;
            }
            // 重叠只允许不变贡献；不以两个删除面集合不相交代替数值对应
            for (const auto *proposal : proposals)
            {
                work.Touch();
                const auto old = Faces(state, *proposal, domain == 1, false);
                const auto next = Faces(state, *proposal, domain == 1, true);
                const auto a = Cover(state, samples, sid, domain == 1, old);
                const auto b = Cover(state, samples, sid, domain == 1, next);
                if (!a || !b)
                {
                    throw std::runtime_error("共享样本证据缺失");
                }
                const auto ref = Reference<R>(state, samples, sid);
                const auto uv = Coordinate(ref, state.Config(), domain == 1);
                if (Height(uv[0], uv[1], (*a)[0], (*a)[1], (*a)[2]) != Height(uv[0], uv[1], (*b)[0], (*b)[1], (*b)[2]))
                {
                    throw std::runtime_error("批次共享质量贡献发生改变");
                }
            }
        }
    }
    work.Seconds["pointwise_batch"] +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}
} // namespace ParallelRoam::Algorithms::GreedyTransactionalLod
