#include <Windows.h>

// D3D12 加载器在创建设备前读取这些导出符号以选择固定运行时
extern "C"
{
__declspec(dllexport) extern const UINT D3D12SDKVersion = PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
