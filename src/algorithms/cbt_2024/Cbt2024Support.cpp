#include "algorithms/cbt_2024/Cbt2024Support.h"

#include "render/GraphicsBackend.h"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
// 可用性判断保持无状态，设备重建后调用方可以立即获得新的能力结果
// 返回对象持有自己的字符串，不依赖后端内部诊断缓存
Cbt2024Availability QueryCbt2024Availability(const Render::IGraphicsBackend& backend)
{
    // 算法层负责组合运行要求，图形后端只报告与算法无关的设备能力
    // 这样新增 CBT 变体时不需要继续扩展 IGraphicsBackend
    if (backend.Api() != Render::GraphicsApi::Direct3D12)
    {
        return Cbt2024Availability{false, "CBT 2024 当前仅支持 D3D12 后端"};
    }

    const Render::GraphicsDeviceCapabilities& capabilities = backend.GraphicsCapabilities();
    // 一次收集全部缺失项，避免用户逐次修复后才看到下一个阻塞条件
    std::vector<std::string_view> missingCapabilities;
    if (capabilities.ShaderModelMajor < 6U ||
        (capabilities.ShaderModelMajor == 6U && capabilities.ShaderModelMinor < 6U))
    {
        missingCapabilities.emplace_back("Shader Model 6.6");
    }
    if (!capabilities.SupportsShaderInt64)
    {
        missingCapabilities.emplace_back("64 位 shader 整数运算");
    }
    if (!capabilities.SupportsTypedResourceInt64Atomics)
    {
        missingCapabilities.emplace_back("64 位 typed resource 原子操作");
    }

    // Available 为 true 时原因必须为空，GUI 不需要额外清理历史错误文本
    if (missingCapabilities.empty())
    {
        return Cbt2024Availability{true, {}};
    }

    // 不可用原因同时供 GUI 和自动测试消费，必须保持完整且可直接诊断
    std::string reason{"CBT 2024 不可用，缺少："};
    for (std::size_t index = 0; index < missingCapabilities.size(); ++index)
    {
        if (index > 0U)
        {
            reason += "、";
        }
        reason += missingCapabilities[index];
    }
    return Cbt2024Availability{false, std::move(reason)};
}
} // namespace ParallelRoam::Algorithms::Cbt2024
