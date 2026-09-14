#pragma once

#include "terrain/TerrainMeshBuilder.h"
#include <array>
#include <bit>
#include <fstream>
#include <stdexcept>

namespace ParallelRoam::Experiment::MeshQuality
{
/// <summary>
/// 有限平台诊断保存实际 float 输出与原生矩阵，禁止序列化结构体 padding
/// 文件不参与正常更新；读取后的网格归离线调用者所有
/// </summary>
struct PlatformMeshArtifact
{
    Terrain::TerrainMeshData Mesh;
    glm::mat4 Matrix{1};
    std::uint32_t Width{}, Height{}, ZeroToOne{};
};

/// <summary>
/// 按固定顺序提取公开顶点字段，不读取对齐填充字节
/// </summary>
inline std::array<float,13> ArtifactVertex(const Terrain::TerrainMeshVertex& v)
{
    return {v.Position.x,v.Position.y,v.Position.z,v.Normal.x,v.Normal.y,v.Normal.z,
        v.TexCoord.x,v.TexCoord.y,v.Height,v.DebugColor.x,v.DebugColor.y,v.DebugColor.z,v.DebugHighlight};
}

/// <summary>
/// 按字段编码诊断身份，覆盖空槽及属性；只在帧计时结束后调用
/// </summary>
inline std::uint64_t PlatformMeshHash(const Terrain::TerrainMeshData& mesh)
{
    std::uint64_t hash=14695981039346656037ULL;
    const auto append=[&](std::uint64_t value) {
        for (unsigned i=0;i<8;++i) { hash^=value&255;hash*=1099511628211ULL;value>>=8; }
    };
    append(mesh.Vertices.size());append(mesh.Indices.size());
    for (const auto& v : mesh.Vertices) for (float f : ArtifactVertex(v)) append(std::bit_cast<std::uint32_t>(f));
    for (auto index : mesh.Indices) append(index);
    return hash;
}

/// <summary>
/// 显式导出小端字段与矩阵，拒绝覆盖已有证据文件
/// </summary>
inline void WritePlatformMesh(const std::filesystem::path& path,const Terrain::TerrainMeshData& mesh,
    const glm::mat4& matrix,std::uint32_t width,std::uint32_t height,bool zeroToOne)
{
    static_assert(std::endian::native==std::endian::little && sizeof(float)==4);
    if (std::filesystem::exists(path)) throw std::runtime_error("Mesh artifact already exists");
    std::ofstream out(path,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);
    const auto write=[&](auto value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); };
    out.write("TPIMSH01",8);write(std::uint64_t(mesh.Vertices.size()));write(std::uint64_t(mesh.Indices.size()));
    write(mesh.TerrainSize);write(mesh.HeightScale);write(width);write(height);write(std::uint32_t(zeroToOne));
    for (glm::length_t r=0;r<4;++r) for (glm::length_t c=0;c<4;++c) write(matrix[c][r]);
    for (const auto& v : mesh.Vertices) for (float f : ArtifactVertex(v)) write(f);
    for (auto i : mesh.Indices) write(i);
}

/// <summary>
/// 限额读取诊断快照；索引和几何合法性继续交给独立评价器核查
/// </summary>
inline PlatformMeshArtifact ReadPlatformMesh(const std::filesystem::path& path)
{
    static_assert(std::endian::native==std::endian::little);
    std::ifstream in(path,std::ios::binary);in.exceptions(std::ios::badbit|std::ios::failbit);
    const auto read=[&]<typename T>(T& value) { in.read(reinterpret_cast<char*>(&value),sizeof(value)); };
    std::array<char,8> magic{};in.read(magic.data(),8);
    if (std::string_view(magic.data(),8)!="TPIMSH01") throw std::runtime_error("Invalid mesh artifact");
    std::uint64_t nv{},ni{};read(nv);read(ni);
    if (nv>2000000 || ni>600000 || ni%3) throw std::runtime_error("Mesh artifact quota exceeded");
    PlatformMeshArtifact result;auto& mesh=result.Mesh;
    read(mesh.TerrainSize);read(mesh.HeightScale);read(result.Width);read(result.Height);read(result.ZeroToOne);
    for (glm::length_t r=0;r<4;++r) for (glm::length_t c=0;c<4;++c) read(result.Matrix[c][r]);
    mesh.Vertices.resize(static_cast<std::size_t>(nv));mesh.Indices.resize(static_cast<std::size_t>(ni));
    for (auto& v : mesh.Vertices)
    {
        std::array<float,13> f{};for (auto& value : f) read(value);
        v.Position={f[0],f[1],f[2]};v.Normal={f[3],f[4],f[5]};v.TexCoord={f[6],f[7]};
        v.Height=f[8];v.DebugColor={f[9],f[10],f[11]};v.DebugHighlight=f[12];
    }
    for (auto& i : mesh.Indices) read(i);
    return result;
}
}
