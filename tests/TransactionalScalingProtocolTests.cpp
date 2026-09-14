#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
/// <summary>
/// 检查冻结协议；失败时直接结束夹具，不依赖构建类型中的断言开关。
/// </summary>
void Require(bool condition)
{
    if (!condition) throw std::runtime_error("压力协议断言失败");
}
/// <summary>
/// 非冻结输入必须显式拒绝，不能退化为另一套实验参数。
/// </summary>
template<class Function> void Reject(Function function)
{
    bool rejected=false;
    try { function(); } catch (const std::invalid_argument&) { rejected=true; }
    Require(rejected);
}
}

int main()
{
    try
    {
        for (auto budget : TransactionalScalingProtocol::Budgets)
        {
            const auto scenario=TransactionalScalingProtocol::Scenario(".",budget,8);
            Require(TransactionalScalingProtocol::Budget(scenario.ScenarioId)==budget);
            Require(scenario.Settings.TerrainSize==80 && scenario.Settings.HeightScale==12);
            Require(scenario.Settings.PassPolicy.SplitScoreWorkerCount==8);
            Configuration config;config.Scenario=scenario.ScenarioId;config.Budget=budget;
            config.TerrainSize=80;config.HeightScale=12;config.SplitPixels=.25;
            config=TransactionalScalingProtocol::View(config,14);
            TransactionalScalingProtocol::ConfigureLimits(config,"fixed64");
            Require(config.PrefixLimit==64 && config.DonorLimit==64);
            TransactionalScalingProtocol::ConfigureLimits(config,"scaled");
            Require(config.PrefixLimit==64*budget/20000 && config.PrefixLimit==config.DonorLimit);
            for (auto index : TransactionalScalingProtocol::ViewOrder)
            {
                const auto next=TransactionalScalingProtocol::View(config,index);
                TransactionalScalingProtocol::Validate(next);
                Require(next.PrefixLimit==config.PrefixLimit && next.DonorLimit==config.DonorLimit);
            }
            Reject([&] { TransactionalScalingProtocol::ConfigureLimits(config,"adaptive"); });
            config.Matrix[0]+=1;
            Reject([&] { TransactionalScalingProtocol::Validate(config); });
        }
        Require(!TransactionalScalingProtocol::Budget("peking547-a-b20000"));
        Require(!TransactionalScalingProtocol::Budget("peking547-sve-orbit64-b20000x"));
        Reject([] { TransactionalScalingProtocol::Camera(64); });
        Reject([] { TransactionalScalingProtocol::Scenario(".",40000,4); });
        Reject([] { TransactionalScalingProtocol::Scenario(".",20000,16); });
        std::cout<<"压力协议夹具通过\n";
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
