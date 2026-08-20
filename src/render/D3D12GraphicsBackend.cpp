#include "render/D3D12GraphicsBackend.h"

#include "gui/ImGuiLayer.h"
#include "tools/PerformanceTimer.h"

#include <SDL_syswm.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>

namespace ParallelRoam::Render
{
namespace
{
// 后端内部格式固定，renderer 和 ImGui 从公开访问器取得同一约定
constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
// 当前固定堆覆盖地形纹理、GPU 算法和 ImGui 的描述符需求
constexpr std::uint32_t SrvDescriptorCount = 256;

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

std::string WideToUtf8(const wchar_t* text)
{
    // DXGI 适配器描述使用宽字符，实验输出统一保存 UTF-8
    if (text == nullptr || text[0] == L'\0')
    {
        return "Unknown D3D12 adapter";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1)
    {
        return "Unknown D3D12 adapter";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string HResultText(HRESULT result)
{
    char* messageBuffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&messageBuffer),
        0,
        nullptr);

    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << static_cast<unsigned long>(result);
    if (length > 0 && messageBuffer != nullptr)
    {
        stream << ": " << messageBuffer;
        LocalFree(messageBuffer);
    }
    return stream.str();
}

std::string LoadedD3D12RuntimeDescription()
{
    const HMODULE runtimeModule = GetModuleHandleW(L"D3D12Core.dll");
    if (runtimeModule == nullptr)
    {
        return "D3D12Core unavailable";
    }

    std::array<wchar_t, 32768> runtimePath{};
    const DWORD pathLength = GetModuleFileNameW(
        runtimeModule,
        runtimePath.data(),
        static_cast<DWORD>(runtimePath.size()));
    const std::string path = pathLength > 0 ? WideToUtf8(runtimePath.data()) : "unknown path";

    DWORD unusedHandle = 0;
    const DWORD versionDataSize = GetFileVersionInfoSizeW(runtimePath.data(), &unusedHandle);
    if (versionDataSize == 0)
    {
        return path;
    }

    std::vector<std::uint8_t> versionData(versionDataSize);
    if (!GetFileVersionInfoW(runtimePath.data(), 0, versionDataSize, versionData.data()))
    {
        return path;
    }

    VS_FIXEDFILEINFO* versionInfo = nullptr;
    UINT versionInfoSize = 0;
    if (!VerQueryValueW(
            versionData.data(),
            L"\\",
            reinterpret_cast<void**>(&versionInfo),
            &versionInfoSize) ||
        versionInfo == nullptr || versionInfoSize < sizeof(VS_FIXEDFILEINFO))
    {
        return path;
    }

    std::ostringstream stream;
    stream << HIWORD(versionInfo->dwFileVersionMS) << '.'
           << LOWORD(versionInfo->dwFileVersionMS) << '.'
           << HIWORD(versionInfo->dwFileVersionLS) << '.'
           << LOWORD(versionInfo->dwFileVersionLS) << " (" << path << ')';
    return stream.str();
}

void SetError(std::string* errorMessage, const char* operation, HRESULT result)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = std::string{operation} + " failed: " + HResultText(result);
    }
}
} // namespace

D3D12GraphicsBackend::~D3D12GraphicsBackend()
{
    Shutdown();
}

GraphicsApi D3D12GraphicsBackend::Api() const
{
    return GraphicsApi::Direct3D12;
}

const char* D3D12GraphicsBackend::Name() const
{
    return "D3D12";
}

bool D3D12GraphicsBackend::ConfigureWindow(std::string*)
{
    return true;
}

std::uint32_t D3D12GraphicsBackend::RequiredSdlWindowFlags() const
{
    return SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
}

bool D3D12GraphicsBackend::Initialize(SDL_Window* window, std::string* errorMessage)
{
    // 初始化失败必须保持对象可再次 Initialize
    if (_initialized || window == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = _initialized ? "D3D12 backend is already initialized" : "D3D12 requires a valid SDL window";
        }
        return false;
    }

    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    // 交换链需要 HWND，SDL 仍负责窗口和事件生命周期
    if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE || windowInfo.subsystem != SDL_SYSWM_WINDOWS)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GetWindowWMInfo failed for D3D12: "} + SDL_GetError();
        }
        return false;
    }

    // 从这里开始 Shutdown 必须能清理部分初始化状态
    _window = window;
    _windowHandle = windowInfo.info.win.window;
    RefreshDrawableSize();
    // 最小化状态也先以一像素创建交换链，实际帧在尺寸恢复前跳过
    _swapChainWidth = static_cast<std::uint32_t>(std::max(_drawableWidth, 1));
    _swapChainHeight = static_cast<std::uint32_t>(std::max(_drawableHeight, 1));

    // 创建顺序体现依赖关系，失败时 Shutdown 按逆序清理部分状态
    if (!CreateDeviceAndQueue(errorMessage) ||
        !CreateSwapChain(errorMessage) ||
        !CreateDescriptorHeaps(errorMessage) ||
        !CreateFrameResources(errorMessage) ||
        !CreateRenderTargets(errorMessage) ||
        !CreateTimestampResources(errorMessage))
    {
        Shutdown();
        return false;
    }

    _initialized = true;
    std::cout << "Graphics backend: D3D12\n";
    std::cout << "D3D12 adapter: " << _adapterName << '\n';
    std::cout << "D3D12 mode: " << _versionString << '\n';
    return true;
}

bool D3D12GraphicsBackend::InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage)
{
    // ImGui 后端要求字体描述符在整个上下文生命周期内保持不变
    _imguiFontDescriptor = AllocateSrvDescriptor();
    if (!_imguiFontDescriptor.IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "D3D12 SRV heap has no descriptor available for Dear ImGui";
        }
        return false;
    }

    // ImGui 使用后端现有队列和共享 SRV 堆，帧数必须与交换链一致
    const Gui::ImGuiD3D12BackendConfig config{
        .Window = _window,
        .Device = _device.Get(),
        .CommandQueue = _commandQueue.Get(),
        .SrvDescriptorHeap = _srvHeap.Get(),
        .FontSrvCpuDescriptor = _imguiFontDescriptor.Cpu,
        .FontSrvGpuDescriptor = _imguiFontDescriptor.Gpu,
        .RenderTargetFormat = BackBufferFormat,
        .DepthStencilFormat = DepthBufferFormat,
        .FramesInFlight = static_cast<int>(FrameCount),
    };
    if (guiLayer.Initialize(config))
    {
        return true;
    }

    ReleaseSrvDescriptor(_imguiFontDescriptor);
    if (errorMessage != nullptr)
    {
        *errorMessage = "Dear ImGui D3D12 backend initialization failed";
    }
    return false;
}

void D3D12GraphicsBackend::WaitForGpuIdle()
{
    if (_commandQueue == nullptr || _fence == nullptr || _fenceEvent == nullptr)
    {
        return;
    }
    std::string errorMessage;
    if (!SignalAndWait(&errorMessage) && !errorMessage.empty())
    {
        std::cerr << errorMessage << '\n';
    }
}

void D3D12GraphicsBackend::Shutdown()
{
    // 资源释放前确保 GPU 不再引用任何后端对象
    if (_commandQueue != nullptr && _fence != nullptr && _fenceEvent != nullptr)
    {
        std::string ignoredError;
        if (!SignalAndWait(&ignoredError) && !ignoredError.empty())
        {
            std::cerr << ignoredError << '\n';
        }
    }

    _frameOpen = false;
    // 描述符先归还，随后统一销毁其所属堆
    ReleaseSrvDescriptor(_imguiFontDescriptor);
    ReleaseRenderTargets();
    _timestampReadback.Reset();
    _timestampQueryHeap.Reset();
    _commandList.Reset();
    for (FrameResource& frame : _frames)
    {
        // allocator 的 FenceValue 与对象一同失效
        frame.CommandAllocator.Reset();
        frame.FenceValue = 0;
    }
    _fence.Reset();
    if (_fenceEvent != nullptr)
    {
        CloseHandle(_fenceEvent);
        _fenceEvent = nullptr;
    }
    _srvHeap.Reset();
    _dsvHeap.Reset();
    _rtvHeap.Reset();
    _swapChain.Reset();
    _commandQueue.Reset();
    _device.Reset();
    _adapter.Reset();
    _factory.Reset();
    _freeSrvIndices.clear();
    _window = nullptr;
    _windowHandle = nullptr;
    _drawableWidth = 0;
    _drawableHeight = 0;
    _swapChainWidth = 0;
    _swapChainHeight = 0;
    _frameIndex = 0;
    _nextFenceValue = 1;
    _lastGpuFrameMilliseconds = 0.0F;
    _lastGpuWaitMilliseconds = 0.0F;
    _adapterName.clear();
    _initialized = false;
}

void D3D12GraphicsBackend::BeginFrame()
{
    // 一个 BeginFrame 只能对应一次 Present
    if (!_initialized || _frameOpen)
    {
        return;
    }

    RefreshDrawableSize();
    // 最小化窗口时没有合法渲染尺寸，主循环仍可继续处理事件
    if (_drawableWidth <= 0 || _drawableHeight <= 0)
    {
        return;
    }

    // 以像素尺寸而非逻辑窗口尺寸判断交换链是否需要重建
    if (_swapChainWidth != static_cast<std::uint32_t>(_drawableWidth) ||
        _swapChainHeight != static_cast<std::uint32_t>(_drawableHeight))
    {
        // ResizeSwapChain 内部等待队列并重建所有尺寸相关资源
        std::string resizeError;
        if (!ResizeSwapChain(
                static_cast<std::uint32_t>(_drawableWidth),
                static_cast<std::uint32_t>(_drawableHeight),
                &resizeError))
        {
            std::cerr << resizeError << '\n';
            return;
        }
    }

    std::string waitError;
    // 只等待即将复用的帧 allocator，不阻塞其他在途帧
    if (!WaitForFrame(_frameIndex, &waitError))
    {
        std::cerr << waitError << '\n';
        return;
    }
    // 围栏完成保证该帧时间戳已经写入读回缓冲
    ReadCompletedTimestamp(_frameIndex);

    FrameResource& frame = _frames[_frameIndex];
    // allocator 和 command list 必须在完成围栏之后按此顺序 Reset
    HRESULT result = frame.CommandAllocator->Reset();
    if (FAILED(result))
    {
        ReportFailure("ID3D12CommandAllocator::Reset", result);
        return;
    }
    result = _commandList->Reset(frame.CommandAllocator.Get(), nullptr);
    if (FAILED(result))
    {
        ReportFailure("ID3D12GraphicsCommandList::Reset", result);
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    // 当前交换链缓冲在整个帧记录期间保持 RENDER_TARGET
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _backBuffers[_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);

    // RTV 槽位索引与 back buffer 索引保持一一对应
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(_frameIndex) * _rtvDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
    _commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    // 后端统一清理颜色和深度，具体 renderer 只记录场景命令
    constexpr std::array<float, 4> ClearColor{0.035F, 0.045F, 0.055F, 1.0F};
    _commandList->ClearRenderTargetView(rtv, ClearColor.data(), 0, nullptr);
    _commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

    // viewport 和 scissor 始终覆盖完整交换链目标
    const D3D12_VIEWPORT viewport{
        0.0F,
        0.0F,
        static_cast<float>(_swapChainWidth),
        static_cast<float>(_swapChainHeight),
        0.0F,
        1.0F,
    };
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(_swapChainWidth), static_cast<LONG>(_swapChainHeight)};
    _commandList->RSSetViewports(1, &viewport);
    _commandList->RSSetScissorRects(1, &scissor);
    // 所有 renderer 共享同一个 shader-visible 描述符堆
    ID3D12DescriptorHeap* heaps[] = {_srvHeap.Get()};
    _commandList->SetDescriptorHeaps(1, heaps);

    if (_timestampQueryHeap != nullptr)
    {
        // 时间戳覆盖本帧所有场景和 ImGui 命令
        _commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, _frameIndex * 2U);
    }
    _frameOpen = true;
}

void D3D12GraphicsBackend::BeginImGuiFrame(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.BeginFrame();
}

void D3D12GraphicsBackend::RenderImGui(Gui::ImGuiLayer& guiLayer)
{
    // D3D12 ImGui 后端把绘制命令追加到仍打开的场景命令列表
    if (_frameOpen)
    {
        guiLayer.EndFrame(_commandList.Get());
    }
}

void D3D12GraphicsBackend::Present()
{
    // 最小化或 BeginFrame 失败时没有可提交命令
    if (!_frameOpen)
    {
        return;
    }

    // 只有查询堆和回读目标同时有效时才记录 Resolve
    if (_timestampQueryHeap != nullptr && _timestampReadback != nullptr)
    {
        // Resolve 写入当前帧独占区域，下一次复用该帧时再读取
        const std::uint32_t queryStart = _frameIndex * 2U;
        _commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart + 1U);
        _commandList->ResolveQueryData(
            _timestampQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryStart,
            2,
            _timestampReadback.Get(),
            static_cast<std::uint64_t>(queryStart) * sizeof(std::uint64_t));
    }

    D3D12_RESOURCE_BARRIER barrier{};
    // Present 前必须把交换链缓冲恢复为 PRESENT
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _backBuffers[_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);

    // 命令列表只有成功关闭后才能交给队列执行
    HRESULT result = _commandList->Close();
    if (FAILED(result))
    {
        ReportFailure("ID3D12GraphicsCommandList::Close", result);
        _frameOpen = false;
        return;
    }

    // 当前后端每帧只提交一个包含场景和 ImGui 的直接命令列表
    ID3D12CommandList* lists[] = {_commandList.Get()};
    _commandQueue->ExecuteCommandLists(1, lists);

    // 非 VSync 模式只在设备和交换链都支持时启用 tearing
    const UINT syncInterval = _vSyncEnabled ? 1U : 0U;
    const UINT presentFlags = !_vSyncEnabled && _tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    result = _swapChain->Present(syncInterval, presentFlags);
    if (FAILED(result))
    {
        ReportFailure("IDXGISwapChain::Present", result);
    }

    // 围栏值写回提交时使用的旧 frameIndex
    const std::uint64_t fenceValue = _nextFenceValue++;
    // Signal 排在 Execute 和 Present 后，完成值覆盖本帧全部 GPU 工作
    result = _commandQueue->Signal(_fence.Get(), fenceValue);
    if (FAILED(result))
    {
        ReportFailure("ID3D12CommandQueue::Signal", result);
    }
    else
    {
        _frames[_frameIndex].FenceValue = fenceValue;
    }

    // Present 后交换链索引可能变化，下一帧必须重新查询
    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    _frameOpen = false;
}

void D3D12GraphicsBackend::RefreshDrawableSize()
{
    // D3D12 交换链使用真实像素尺寸以适配 HiDPI 缩放
    if (_window == nullptr)
    {
        _drawableWidth = 0;
        _drawableHeight = 0;
        return;
    }
    SDL_GetWindowSizeInPixels(_window, &_drawableWidth, &_drawableHeight);
}

bool D3D12GraphicsBackend::SetVSyncEnabled(bool enabled)
{
    _vSyncEnabled = enabled;
    return true;
}

bool D3D12GraphicsBackend::VSyncEnabled() const
{
    return _vSyncEnabled;
}

int D3D12GraphicsBackend::DrawableWidth() const
{
    return _drawableWidth;
}

int D3D12GraphicsBackend::DrawableHeight() const
{
    return _drawableHeight;
}

bool D3D12GraphicsBackend::UsesZeroToOneDepth() const
{
    return true;
}

const std::string& D3D12GraphicsBackend::AdapterName() const
{
    return _adapterName;
}

const std::string& D3D12GraphicsBackend::VersionString() const
{
    return _versionString;
}

float D3D12GraphicsBackend::LastGpuFrameMilliseconds() const
{
    return _lastGpuFrameMilliseconds;
}

float D3D12GraphicsBackend::LastGpuWaitMilliseconds() const
{
    return _lastGpuWaitMilliseconds;
}

bool D3D12GraphicsBackend::IsValid() const
{
    return _initialized && _device != nullptr && _swapChain != nullptr;
}

ID3D12Device* D3D12GraphicsBackend::Device() const
{
    return _device.Get();
}

ID3D12CommandQueue* D3D12GraphicsBackend::CommandQueue() const
{
    return _commandQueue.Get();
}

ID3D12GraphicsCommandList* D3D12GraphicsBackend::CommandList() const
{
    // 算法只能在后端已打开帧命令列表时记录 GPU 工作
    return _frameOpen ? _commandList.Get() : nullptr;
}

ID3D12DescriptorHeap* D3D12GraphicsBackend::ShaderVisibleSrvHeap() const
{
    return _srvHeap.Get();
}

DXGI_FORMAT D3D12GraphicsBackend::RenderTargetFormat() const
{
    return BackBufferFormat;
}

DXGI_FORMAT D3D12GraphicsBackend::DepthStencilFormat() const
{
    return DepthBufferFormat;
}

std::uint32_t D3D12GraphicsBackend::CurrentFrameIndex() const
{
    return _frameIndex;
}

bool D3D12GraphicsBackend::FrameOpen() const
{
    return _frameOpen;
}

D3D12DescriptorAllocation D3D12GraphicsBackend::AllocateSrvDescriptor()
{
    // 固定堆耗尽时由调用方决定降级或报告错误
    if (_srvHeap == nullptr || _freeSrvIndices.empty())
    {
        return {};
    }

    // 槽位一经分配在显式归还前不会移动
    D3D12DescriptorAllocation allocation{};
    // CPU/GPU 句柄必须由同一索引和各自堆起点计算
    allocation.Index = _freeSrvIndices.back();
    _freeSrvIndices.pop_back();
    allocation.Cpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
    allocation.Gpu = _srvHeap->GetGPUDescriptorHandleForHeapStart();
    allocation.Cpu.ptr += static_cast<SIZE_T>(allocation.Index) * _srvDescriptorSize;
    allocation.Gpu.ptr += static_cast<UINT64>(allocation.Index) * _srvDescriptorSize;
    return allocation;
}

void D3D12GraphicsBackend::ReleaseSrvDescriptor(D3D12DescriptorAllocation& allocation)
{
    if (!allocation.IsValid())
    {
        return;
    }
    // 调用方句柄清空可阻止重复释放同一槽位
    _freeSrvIndices.push_back(allocation.Index);
    allocation = {};
}

bool D3D12GraphicsBackend::ExecuteImmediate(
    const std::function<bool(ID3D12GraphicsCommandList*, std::string*)>& recorder,
    std::string* errorMessage)
{
    // 独立 allocator 避免破坏正在记录或等待的帧资源
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    HRESULT result = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandAllocator for immediate upload", result);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    result = _device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandList for immediate upload", result);
        return false;
    }

    if (!recorder(commandList.Get(), errorMessage))
    {
        // recorder 负责提供比通用 HRESULT 更具体的资源错误
        return false;
    }

    result = commandList->Close();
    if (FAILED(result))
    {
        SetError(errorMessage, "Close immediate command list", result);
        return false;
    }
    // 立即路径的局部列表单独提交但仍进入共享围栏序列
    ID3D12CommandList* lists[] = {commandList.Get()};
    _commandQueue->ExecuteCommandLists(1, lists);
    // 局部 COM 对象离开函数前必须确认 GPU 已消费命令
    return SignalAndWait(errorMessage);
}

bool D3D12GraphicsBackend::CreateDeviceAndQueue(std::string* errorMessage)
{
    UINT factoryFlags = 0;
#if !defined(NDEBUG)
    // Debug 层必须在创建设备前启用
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // DXGI factory 负责适配器枚举 交换链创建和呈现能力查询
    HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&_factory));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateDXGIFactory2", result);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(_factory.As(&factory5)))
    {
        // tearing 是可选呈现能力，不影响基础 DX12 初始化
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing,
                sizeof(allowTearing))))
        {
            _tearingSupported = allowTearing == TRUE;
        }
    }

    // 按高性能偏好枚举并跳过软件适配器
    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        result = _factory->EnumAdapterByGpuPreference(
            adapterIndex,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result))
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 description{};
        candidate->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }
        // 探测调用不创建设备，只验证最低功能级
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            _adapter = candidate;
            _adapterName = WideToUtf8(description.Description);
            break;
        }
    }

    // 不回退 WARP，避免基准误把软件实现记录为 GPU 结果
    if (_adapter == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "No hardware adapter supports D3D12 feature level 12_0";
        }
        return false;
    }

    LARGE_INTEGER driverVersion{};
    // 驱动版本只用于日志和 benchmark，不作为能力判断依据
    if (SUCCEEDED(_adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)))
    {
        std::ostringstream versionStream;
        versionStream << "Direct3D 12 (feature level 12_0); requested Agility SDK "
                      << PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION << "; runtime "
                      << LoadedD3D12RuntimeDescription() << "; driver "
                      << HIWORD(driverVersion.HighPart) << '.'
                      << LOWORD(driverVersion.HighPart) << '.'
                      << HIWORD(driverVersion.LowPart) << '.'
                      << LOWORD(driverVersion.LowPart);
        _versionString = versionStream.str();
    }
    else
    {
        _versionString =
            "Direct3D 12 (feature level 12_0); requested Agility SDK " +
            std::to_string(PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION) + "; runtime " +
            LoadedD3D12RuntimeDescription();
    }

    result = D3D12CreateDevice(_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_device));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12CreateDevice", result);
        return false;
    }

#if !defined(NDEBUG)
    // 严重验证错误在开发构建中立即中断
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(_device.As(&infoQueue)))
    {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    }
#endif

    // 单 direct queue 保证计算生成网格与后续图形读取天然有序
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    // 当前 graphics/compute/copy 工作统一记录到 direct queue
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    result = _device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&_commandQueue));
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateCommandQueue", result);
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::CreateSwapChain(std::string* errorMessage)
{
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = _swapChainWidth;
    description.Height = _swapChainHeight;
    description.Format = BackBufferFormat;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // 缓冲数量必须与 FrameResource 数量一致
    description.BufferCount = FrameCount;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Flags = _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    HRESULT result = _factory->CreateSwapChainForHwnd(
        _commandQueue.Get(),
        _windowHandle,
        &description,
        nullptr,
        nullptr,
        &swapChain);
    if (FAILED(result))
    {
        SetError(errorMessage, "CreateSwapChainForHwnd", result);
        return false;
    }
    // 禁止 DXGI 接管 Alt+Enter，窗口模式由 SDL 管理
    _factory->MakeWindowAssociation(_windowHandle, DXGI_MWA_NO_ALT_ENTER);
    result = swapChain.As(&_swapChain);
    if (FAILED(result))
    {
        SetError(errorMessage, "Query IDXGISwapChain3", result);
        return false;
    }
    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12GraphicsBackend::CreateDescriptorHeaps(std::string* errorMessage)
{
    // 每个交换链缓冲占用一个 RTV 槽位
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescription.NumDescriptors = FrameCount;
    HRESULT result = _device->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&_rtvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create RTV descriptor heap", result);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
    // 所有帧共享一个随交换链尺寸重建的深度缓冲
    dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDescription.NumDescriptors = 1;
    result = _device->CreateDescriptorHeap(&dsvDescription, IID_PPV_ARGS(&_dsvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create DSV descriptor heap", result);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDescription{};
    // renderer 和算法从同一可见堆分配稳定资源视图
    srvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescription.NumDescriptors = SrvDescriptorCount;
    srvDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = _device->CreateDescriptorHeap(&srvDescription, IID_PPV_ARGS(&_srvHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create shader-visible SRV descriptor heap", result);
        return false;
    }

    _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    _srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    _freeSrvIndices.reserve(SrvDescriptorCount);
    // 逆序填充使首次分配从索引零开始
    for (std::uint32_t index = SrvDescriptorCount; index > 0; --index)
    {
        _freeSrvIndices.push_back(index - 1U);
    }
    return true;
}

bool D3D12GraphicsBackend::CreateFrameResources(std::string* errorMessage)
{
    // allocator 不能在对应 GPU 工作完成前复用
    for (FrameResource& frame : _frames)
    {
        const HRESULT result = _device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame.CommandAllocator));
        if (FAILED(result))
        {
            SetError(errorMessage, "Create frame command allocator", result);
            return false;
        }
    }

    HRESULT result = _device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        _frames[0].CommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&_commandList));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create graphics command list", result);
        return false;
    }
    // 新建命令列表默认处于 recording 状态，初始化后先关闭
    result = _commandList->Close();
    if (FAILED(result))
    {
        SetError(errorMessage, "Close initial graphics command list", result);
        return false;
    }

    result = _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create frame fence", result);
        return false;
    }
    // 自动重置事件供所有帧等待顺序复用
    _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (_fenceEvent == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "CreateEvent failed for D3D12 frame fence";
        }
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::CreateRenderTargets(std::string* errorMessage)
{
    // RTV 描述符按交换链缓冲索引连续排列
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t index = 0; index < FrameCount; ++index)
    {
        HRESULT result = _swapChain->GetBuffer(index, IID_PPV_ARGS(&_backBuffers[index]));
        if (FAILED(result))
        {
            SetError(errorMessage, "Get swap-chain back buffer", result);
            return false;
        }
        _device->CreateRenderTargetView(_backBuffers[index].Get(), nullptr, rtv);
        rtv.ptr += _rtvDescriptorSize;
    }

    // 深度资源尺寸必须始终与交换链一致
    D3D12_RESOURCE_DESC depthDescription{};
    depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDescription.Width = _swapChainWidth;
    depthDescription.Height = _swapChainHeight;
    depthDescription.DepthOrArraySize = 1;
    depthDescription.MipLevels = 1;
    depthDescription.Format = DepthBufferFormat;
    depthDescription.SampleDesc.Count = 1;
    depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clearValue{};
    // 优化清除值与每帧 ClearDepthStencilView 保持一致
    clearValue.Format = DepthBufferFormat;
    clearValue.DepthStencil.Depth = 1.0F;
    HRESULT result = _device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthDescription,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&_depthBuffer));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 depth buffer", result);
        return false;
    }
    _device->CreateDepthStencilView(_depthBuffer.Get(), nullptr, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12GraphicsBackend::CreateTimestampResources(std::string* errorMessage)
{
    // 每个帧槽位保存开始和结束两个时间戳
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDescription.Count = FrameCount * 2U;
    HRESULT result = _device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&_timestampQueryHeap));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 timestamp query heap", result);
        return false;
    }

    // 共享读回缓冲按 frameIndex 划分互不覆盖的区域
    D3D12_RESOURCE_DESC readbackDescription{};
    readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescription.Width = FrameCount * 2U * sizeof(std::uint64_t);
    readbackDescription.Height = 1;
    readbackDescription.DepthOrArraySize = 1;
    readbackDescription.MipLevels = 1;
    readbackDescription.SampleDesc.Count = 1;
    readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_READBACK);
    result = _device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &readbackDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&_timestampReadback));
    if (FAILED(result))
    {
        SetError(errorMessage, "Create D3D12 timestamp readback buffer", result);
        return false;
    }

    result = _commandQueue->GetTimestampFrequency(&_timestampFrequency);
    if (FAILED(result))
    {
        SetError(errorMessage, "Get D3D12 timestamp frequency", result);
        return false;
    }
    return true;
}

bool D3D12GraphicsBackend::ResizeSwapChain(std::uint32_t width, std::uint32_t height, std::string* errorMessage)
{
    if (width == 0 || height == 0)
    {
        return true;
    }
    // ResizeBuffers 要求所有旧 back buffer 引用已不再被 GPU 使用
    if (!SignalAndWait(errorMessage))
    {
        return false;
    }
    ReleaseRenderTargets();
    const UINT flags = _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;
    const HRESULT result = _swapChain->ResizeBuffers(FrameCount, width, height, BackBufferFormat, flags);
    if (FAILED(result))
    {
        SetError(errorMessage, "Resize D3D12 swap chain", result);
        return false;
    }
    _swapChainWidth = width;
    _swapChainHeight = height;
    // ResizeBuffers 可能改变当前缓冲索引
    _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    return CreateRenderTargets(errorMessage);
}

bool D3D12GraphicsBackend::WaitForFrame(std::uint32_t frameIndex, std::string* errorMessage)
{
    _lastGpuWaitMilliseconds = 0.0F;
    const std::uint64_t fenceValue = _frames[frameIndex].FenceValue;
    // 从未提交或已经完成的帧资源可直接复用
    if (fenceValue == 0 || _fence->GetCompletedValue() >= fenceValue)
    {
        return true;
    }
    HRESULT result = _fence->SetEventOnCompletion(fenceValue, _fenceEvent);
    if (FAILED(result))
    {
        SetError(errorMessage, "Set frame fence event", result);
        return false;
    }
    // 该等待只覆盖当前 frame allocator 的所有权冲突
    Tools::PerformanceTimer waitTimer;
    WaitForSingleObject(_fenceEvent, INFINITE);
    _lastGpuWaitMilliseconds = waitTimer.Stop();
    return true;
}

bool D3D12GraphicsBackend::SignalAndWait(std::string* errorMessage)
{
    // 全队列同步只用于资源销毁、缩放和立即提交
    const std::uint64_t fenceValue = _nextFenceValue++;
    HRESULT result = _commandQueue->Signal(_fence.Get(), fenceValue);
    if (FAILED(result))
    {
        SetError(errorMessage, "Signal D3D12 fence", result);
        return false;
    }
    result = _fence->SetEventOnCompletion(fenceValue, _fenceEvent);
    if (FAILED(result))
    {
        SetError(errorMessage, "Set D3D12 fence event", result);
        return false;
    }
    WaitForSingleObject(_fenceEvent, INFINITE);
    return true;
}

void D3D12GraphicsBackend::ReadCompletedTimestamp(std::uint32_t frameIndex)
{
    // 调用方已等待该 frameIndex 的围栏，因此 Map 不会读取在途写入
    if (_timestampReadback == nullptr || _timestampFrequency == 0 || _frames[frameIndex].FenceValue == 0)
    {
        return;
    }

    const std::uint64_t offset = static_cast<std::uint64_t>(frameIndex) * 2U * sizeof(std::uint64_t);
    // readRange 限制调试层和驱动只同步当前时间戳对
    D3D12_RANGE readRange{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + 2U * sizeof(std::uint64_t))};
    void* mappedData = nullptr;
    if (SUCCEEDED(_timestampReadback->Map(0, &readRange, &mappedData)))
    {
        const auto* timestamps = static_cast<const std::uint64_t*>(mappedData);
        const std::uint64_t* pair = timestamps + frameIndex * 2U;
        if (pair[1] >= pair[0])
        {
            // 时间戳频率是每秒 tick 数，结果统一输出毫秒
            _lastGpuFrameMilliseconds =
                static_cast<float>(static_cast<double>(pair[1] - pair[0]) * 1000.0 /
                                   static_cast<double>(_timestampFrequency));
        }
        const D3D12_RANGE writeRange{0, 0};
        _timestampReadback->Unmap(0, &writeRange);
    }
}

void D3D12GraphicsBackend::ReleaseRenderTargets()
{
    // 描述符堆保留，Resize 后在相同槽位重建视图
    _depthBuffer.Reset();
    for (auto& backBuffer : _backBuffers)
    {
        backBuffer.Reset();
    }
}

void D3D12GraphicsBackend::ReportFailure(const char* operation, HRESULT result) const
{
    std::cerr << operation << " failed: " << HResultText(result);
    if (_device != nullptr)
    {
        // 设备移除原因通常比触发失败的 API 更接近根因
        const HRESULT removedReason = _device->GetDeviceRemovedReason();
        if (FAILED(removedReason))
        {
            std::cerr << "\nD3D12 device removed reason: " << HResultText(removedReason);
        }
    }
    std::cerr << '\n';
}
} // namespace ParallelRoam::Render
