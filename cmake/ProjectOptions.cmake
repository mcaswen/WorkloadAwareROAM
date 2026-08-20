option(PARALLEL_ROAM_BUILD_APP "Build the Parallel ROAM application target." ON)
option(PARALLEL_ROAM_BUILD_TESTS "Build tests when tests/CMakeLists.txt exists." OFF)

set(PARALLEL_ROAM_GRAPHICS_API "OpenGL" CACHE STRING "Graphics backend selected at configure time.")
set_property(CACHE PARALLEL_ROAM_GRAPHICS_API PROPERTY STRINGS OpenGL D3D12)

option(PARALLEL_ROAM_WITH_SDL2 "Try to link SDL2 when it is available." ON)
option(PARALLEL_ROAM_WITH_OPENGL "Try to link OpenGL when it is available." ON)
option(PARALLEL_ROAM_WITH_GLM "Try to link GLM when it is available." ON)
option(PARALLEL_ROAM_WITH_GLAD "Try to link GLAD when it is available." ON)
option(PARALLEL_ROAM_WITH_STB "Try to link stb headers when they are available." ON)
option(PARALLEL_ROAM_WITH_IMGUI "Try to link Dear ImGui when it is available." ON)
option(PARALLEL_ROAM_FETCH_MISSING_DEPS "Download missing dependencies with FetchContent." OFF)

set(PARALLEL_ROAM_SDL2_FETCH_TAG "release-2.32.10" CACHE STRING "SDL2 tag used when FetchContent is enabled.")
set(PARALLEL_ROAM_GLM_FETCH_TAG "1.0.3" CACHE STRING "GLM tag used when FetchContent is enabled.")
set(PARALLEL_ROAM_GLAD_FETCH_TAG "v2.0.8" CACHE STRING "GLAD tag used when FetchContent is enabled.")
set(PARALLEL_ROAM_IMGUI_FETCH_TAG "v1.92.8" CACHE STRING "Dear ImGui tag used when FetchContent is enabled.")
set(PARALLEL_ROAM_STB_FETCH_TAG "31c1ad37456438565541f4919958214b6e762fb4" CACHE STRING "stb commit used when FetchContent is enabled.")

set(PARALLEL_ROAM_D3D12_AGILITY_SDK_PACKAGE_VERSION "1.614.1" CACHE INTERNAL "Pinned D3D12 Agility SDK package version." FORCE)
set(PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION "614" CACHE INTERNAL "D3D12SDKVersion exported by the application." FORCE)
set(
    PARALLEL_ROAM_D3D12_AGILITY_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/microsoft/d3d12-agility-sdk/1.614.1"
    CACHE PATH
    "Root of the pinned D3D12 Agility SDK package."
)
set(PARALLEL_ROAM_DXC_PACKAGE_VERSION "1.7.2308.12" CACHE INTERNAL "Pinned DirectX Shader Compiler package version." FORCE)
set(
    PARALLEL_ROAM_DXC_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/microsoft/dxc/1.7.2308.12"
    CACHE PATH
    "Root of the pinned DirectX Shader Compiler package."
)

option(PARALLEL_ROAM_ENABLE_WARNINGS "Enable project compiler warnings." ON)
option(PARALLEL_ROAM_WARNINGS_AS_ERRORS "Treat project warnings as errors." OFF)
option(PARALLEL_ROAM_ENABLE_SANITIZERS "Enable sanitizer flags for supported compilers." OFF)
option(PARALLEL_ROAM_ENABLE_ASAN "Enable AddressSanitizer when sanitizers are enabled." ON)
option(PARALLEL_ROAM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer when sanitizers are enabled." ON)

macro(parallel_roam_apply_global_options)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_C_STANDARD 17)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    endif()
endmacro()
