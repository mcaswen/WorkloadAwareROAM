#include "algorithms/greedy_transactional_lod/TransactionalPriorityIndex.h"
#include <iostream>
#include <random>

using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;

int main()
{
    using Key=std::pair<double,Identity>;
    using Index=TransactionalPriorityIndex<Key>;
    WorkLedger work;Index index;std::vector<std::optional<Key>> records(777);
    std::mt19937 random(7301);
    for (std::size_t i=0;i<records.size();++i)
        if (i%3) records[i]=Key{static_cast<double>(random()%11),static_cast<Identity>(i)};
    index.Rebuild(records,work);
    const auto check=[&] {
        std::vector<Key> all;
        for (const auto& key : records) if (key) all.push_back(*key);
        std::sort(all.begin(),all.end());
        if (index.Count()!=all.size()) throw std::runtime_error("count mismatch");
        for (auto limit : {std::size_t{0},std::size_t{1},std::size_t{64},all.size()+1})
        {
            const std::vector<Key> expected(all.begin(),all.begin()+static_cast<std::ptrdiff_t>(std::min(limit,all.size())));
            if (index.Prefix(limit,&work)!=expected) throw std::runtime_error("prefix mismatch");
        }
    };
    check();
    // 准备后放弃、删除队首和重用槽都必须与独立全排序一致。
    { auto discarded=index.PrepareRepair({{1,std::nullopt},{900,Key{-1,-1}}},901,work);check(); }
    for (std::size_t step=0;step<40;++step)
    {
        Index::Writes writes;
        const auto slots=records.size()+(step%5==0 ? 17 : 0);
        for (std::size_t j=0;j<25;++j)
        {
            const auto slot=static_cast<Slot>(random()%slots);
            writes[slot]=j%3 ? std::optional<Key>{{static_cast<double>(random()%11),-static_cast<Identity>(step*1000+j+1)}} : std::nullopt;
        }
        auto repair=index.PrepareRepair(writes,slots,work);check();
        records.resize(slots);for (const auto& [slot,key] : writes) records[slot]=key;
        index.Publish(std::move(repair));check();
    }
    Index::Writes clear;
    for (std::size_t i=0;i<records.size();++i) clear[static_cast<Slot>(i)]=std::nullopt;
    auto repair=index.PrepareRepair(clear,records.size(),work);index.Publish(std::move(repair));
    std::fill(records.begin(),records.end(),std::nullopt);check();
    std::cout<<"exact prefix and transactional repair passed\n";
}
