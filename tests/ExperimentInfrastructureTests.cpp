#include "experiment/infrastructure/ExperimentCase.h"
#include "experiment/infrastructure/ExperimentRecords.h"
#include "terrain/HeightMap.h"

#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ParallelRoam::Experiment::Infrastructure;
namespace
{
void Require(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}
}
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "用法: resolved-case 输出根");
        const std::filesystem::path input(argv[1]), output(argv[2]);
        const auto config = ExperimentCase::LoadResolved(input);
        ParallelRoam::Terrain::HeightMap map;
        std::string error;
        Require(map.LoadFromFile(config.HeightMapPath, &error), error.c_str());
        Require(map.Width() == static_cast<int>(config.Width) &&
                map.Height() == static_cast<int>(config.Height), "实际样本尺寸不符");
        std::filesystem::create_directories(output);
        std::ofstream raw(output / "samples.u16", std::ios::binary);
        for (const auto value : map.RawSamples())
        {
            raw.put(static_cast<char>(value & 255));
            raw.put(static_cast<char>(value >> 8));
        }
        raw.close();
        ExperimentRecords records(output / "run");
        records.Write("case", config.Id, input.string());
        bool rejected = false;
        try { ExperimentRecords duplicate(output / "run"); }
        catch (const std::exception&) { rejected = true; }
        Require(rejected, "重复目录必须拒绝");

        // 直接构造损坏配置，检查解析器确实拒绝而不是静默修正
        boost::property_tree::ptree tree;
        boost::property_tree::read_json(input.string(), tree);
        for (const auto& field : {"algorithm", "budget", "workers", "heightScale"})
        {
            auto broken = tree;
            broken.put(field, "invalid");
            const auto path = output / (std::string(field) + ".json");
            boost::property_tree::write_json(path.string(), broken);
            rejected = false;
            try { (void)ExperimentCase::LoadResolved(path); }
            catch (const std::exception&) { rejected = true; }
            Require(rejected, "损坏配置必须拒绝");
        }
        const auto start = std::chrono::steady_clock::now();
        Require(map.LoadFromFile(config.HeightMapPath, &error), error.c_str());
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        records.Write("timing", "directHeightMapLoadMs", std::to_string(ms));
        std::cout << config.Id << " " << map.RawSamples().size() << " loadMs=" << ms << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
