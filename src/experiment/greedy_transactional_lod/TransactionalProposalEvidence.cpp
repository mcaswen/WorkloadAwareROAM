#include "experiment/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include <algorithm>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
TransactionalProposalEvidence::TransactionalProposalEvidence(const TransactionalSamples& samples,
    const Proposal& proposal,WorkLedger& work) : _samples(samples),_proposal(proposal),_entries(proposal.Samples.size())
{
    work.EvidenceBytes+=_entries.size()*sizeof(Entry);
}

const TransactionalProposalEvidence::Entry& TransactionalProposalEvidence::Get(Slot sample,WorkLedger& work)
{
    ++work.EvidenceLookups;
    const auto it=std::lower_bound(_proposal.Samples.begin(),_proposal.Samples.end(),sample);
    if (it==_proposal.Samples.end() || *it!=sample) throw std::runtime_error("样本不属于本提案证据域");
    auto& entry=_entries[static_cast<std::size_t>(it-_proposal.Samples.begin())];
    if (entry.Ready) { ++work.EvidenceHits;return entry; }
    for (std::size_t i=0;i<_proposal.Faces.size();++i)
    {
        const auto& f=_proposal.Faces[i];++work.EvidenceFaceTests;
        if (!_samples.Weights(sample,_proposal.Points.at(f[0]),_proposal.Points.at(f[1]),_proposal.Points.at(f[2]),entry.Weights)) continue;
        entry.Face=i;entry.Ready=true;++work.EvidenceBuilds;return entry;
    }
    throw std::runtime_error("局部提案缺失闭面样本覆盖");
}

std::array<Point,3> TransactionalProposalEvidence::Face(Slot sample,WorkLedger& work)
{
    const auto& f=_proposal.Faces[Get(sample,work).Face];
    return {_proposal.Points.at(f[0]),_proposal.Points.at(f[1]),_proposal.Points.at(f[2])};
}
}
