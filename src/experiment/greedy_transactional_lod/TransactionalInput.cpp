#include "experiment/greedy_transactional_lod/TransactionalInput.h"

#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
InitialMesh TransactionalInput::Load(const std::filesystem::path& snapshot)
{
    using boost::property_tree::ptree;
    ptree data;boost::property_tree::read_json(snapshot.string(),data);
    InitialMesh input;auto& c=input.Config;
    c.Scenario=data.get<std::string>("scenario");c.SampleIndex=data.get<std::uint32_t>("sampleIndex");
    if (c.Scenario!="test129-a-b4096" && c.Scenario!="peking547-a-b20000")
        throw std::runtime_error("仅支持冻结自然来源");
    c.Budget=data.get<std::size_t>("budget");c.TerrainSize=data.get<double>("size");c.HeightScale=data.get<double>("scale");
    c.SplitPixels=data.get<double>("splitPixels");c.Width=data.get<std::uint32_t>("width");c.Height=data.get<std::uint32_t>("height");
    std::size_t index=0;
    for (const auto& entry : data.get_child("matrix"))
    {
        if (index==16) throw std::runtime_error("视图矩阵长度错误");
        c.Matrix[index++]=entry.second.get_value<double>();
    }
    if (index!=16) throw std::runtime_error("视图矩阵长度错误");
    for (const auto& entry : data.get_child("vertices"))
    {
        // 整数身份直接解析，不能先经过 double 丢失路径高位
        const auto& row=entry.second;
        if (row.size()!=4) throw std::runtime_error("顶点行长度错误");
        auto it=row.begin();const auto id=(it++)->second.get_value<Identity>();
        const auto u=(it++)->second.get_value<double>(),v=(it++)->second.get_value<double>(),h=it->second.get_value<double>();
        input.Vertices.push_back({id,{u,v,h}});
    }
    for (const auto& entry : data.get_child("faces"))
    {
        const auto& row=entry.second;if (row.size()!=4) throw std::runtime_error("面行长度错误");
        auto it=row.begin();Triangle face;face.Id=(it++)->second.get_value<Identity>();
        for (auto& id : face.Vertices) id=(it++)->second.get_value<Identity>();
        input.Faces.push_back(face);
    }
    ptree source;boost::property_tree::read_json((snapshot.parent_path()/(c.Scenario+"-source.json")).string(),source);
    // 原始样本独立加载，当前网格拟合高度不能反向污染参考
    input.Source.Width=source.get<std::uint32_t>("width");input.Source.Height=source.get<std::uint32_t>("height");
    for (const auto& entry : source.get_child("values"))
    {
        const auto value=entry.second.get_value<std::uint32_t>();
        if (value>65535) throw std::runtime_error("原始样本超出 uint16");
        input.Source.Values.push_back(static_cast<std::uint16_t>(value));
    }
    return input;
}

void TransactionalInput::Write(const TransactionalState& state,const std::filesystem::path& output)
{
    if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖已存在的几何结果");
    std::ofstream out(output);out<<std::setprecision(17);
    // 足够位数恢复实际 binary64 发布值，文本舍入不应另造待验证曲面
    const auto& c=state.Config();
    out<<"{\"scenario\":"<<std::quoted(c.Scenario)<<",\"sampleIndex\":"<<c.SampleIndex
        <<",\"budget\":"<<c.Budget<<",\"size\":"<<c.TerrainSize<<",\"scale\":"<<c.HeightScale
        <<",\"splitPixels\":"<<c.SplitPixels<<",\"width\":"<<c.Width<<",\"height\":"<<c.Height<<",\"matrix\":[";
    for (std::size_t i=0;i<16;++i) out<<(i ? "," : "")<<c.Matrix[i];
    out<<"],\"vertices\":[";bool first=true;
    for (const auto& vertex : state.Vertices())
    {
        if (!vertex.Active) continue;
        const auto& p=vertex.Geometry;
        out<<(first ? "" : ",")<<'['<<vertex.Id<<','<<p.U<<','<<p.V<<','<<p.Height<<']';first=false;
    }
    out<<"],\"faces\":[";first=true;
    for (auto slot : state.ActiveFaces())
    {
        const auto& face=state.Face(slot);
        out<<(first ? "" : ",")<<'['<<face.Id;
        for (auto id : face.Vertices) out<<','<<id;
        out<<']';first=false;
    }
    out<<"]}";out.close();
    if (!out) throw std::runtime_error("几何结果写入失败");
}
}
