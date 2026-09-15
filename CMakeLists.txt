cmake_minimum_required(VERSION 3.24)

project(
    ParallelROAM
    VERSION 0.1.0
    DESCRIPTION "Real-time adaptive terrain subdivision experiments across Classic and data-oriented CPU ROAM pipelines"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
include(PinnedD3D12Runtime)
include(Profiling)

parallel_roam_apply_global_options()
find_package(Threads REQUIRED)

string(TOUPPER "${PARALLEL_ROAM_GRAPHICS_API}" PARALLEL_ROAM_GRAPHICS_API_NORMALIZED)
if(NOT PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL" AND
   NOT PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12")
    message(FATAL_ERROR "PARALLEL_ROAM_GRAPHICS_API must be OpenGL or D3D12.")
endif()
set(
    PARALLEL_ROAM_GRAPHICS_API_NORMALIZED
    "${PARALLEL_ROAM_GRAPHICS_API_NORMALIZED}"
    CACHE INTERNAL
    "Normalized graphics backend name."
    FORCE
)
message(STATUS "Parallel ROAM graphics backend: ${PARALLEL_ROAM_GRAPHICS_API}")

add_library(parallel_roam_project_options INTERFACE)
target_compile_features(parallel_roam_project_options INTERFACE cxx_std_20)
parallel_roam_prepare_profiling()
# 用项目目标传播设置，核心源码与入口保持一致；关闭时没有客户端依赖
target_link_libraries(parallel_roam_project_options INTERFACE parallel_roam_profiling_options)

if(MSVC)
    target_compile_options(parallel_roam_project_options INTERFACE /utf-8)
endif()

add_library(parallel_roam_project_warnings INTERFACE)
parallel_roam_set_project_warnings(parallel_roam_project_warnings)

if(PARALLEL_ROAM_ENABLE_SANITIZERS)
    parallel_roam_enable_sanitizers(parallel_roam_project_options)
endif()

# DOD 与独立算法共用任务实现；对象库避免每个入口重复编译线程工具
add_library(parallel_roam_cpu_execution OBJECT
    src/tools/CpuThreadPool.cpp src/tools/CpuTaskExecutor.cpp)
target_include_directories(parallel_roam_cpu_execution PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(parallel_roam_cpu_execution PUBLIC parallel_roam_project_options Threads::Threads
    PRIVATE parallel_roam_project_warnings)
include(TransactionalLod)
include(ExperimentInfrastructure)

# 应用和 CPU 执行测试共用同一组 DOD 源码，避免新增文件只接入一个后端
set(PARALLEL_ROAM_DOD_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPassInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamMeshPlan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPassExecution.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamScoring.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp"
    $<TARGET_OBJECTS:parallel_roam_cpu_execution>
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/DataOrientedRoamValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/materialization/NativePlanningView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/materialization/NativePlanningQueues.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/materialization/NativeRefinementSimulation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/data_oriented_roam/materialization/NativeTargetPlanner.cpp"
)

# 两后端和纯 CPU 输入测试复用同一份清单、相机与记录实现
set(PARALLEL_ROAM_FORMAL_INPUT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/formal/FormalExperimentTargetSelector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/ExperimentCsvCodec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/formal/FormalExperimentCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/formal/FormalExperimentManifest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/formal/FormalExperimentCsv.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/experiment/formal/FormalCpuPairCsv.cpp"
)

if(PARALLEL_ROAM_BUILD_APP)
    parallel_roam_prepare_dependencies()

    set(PARALLEL_ROAM_APP_SOURCES src/tools/PerformanceTimer.cpp)
    set(PARALLEL_ROAM_FULL_APP_AVAILABLE OFF)

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL" AND
       TARGET ParallelROAM::OpenGL AND
       TARGET ParallelROAM::SDL2 AND
       TARGET ParallelROAM::GLM AND
       TARGET ParallelROAM::GLAD AND
       TARGET ParallelROAM::ImGui AND
       TARGET ParallelROAM::STB)
        set(PARALLEL_ROAM_FULL_APP_AVAILABLE ON)
        list(
            APPEND
            PARALLEL_ROAM_APP_SOURCES
                src/app/Application.cpp
                src/app/ApplicationCommandLine.cpp
                src/app/CameraController.cpp
                src/app/InputState.cpp
                src/app/RuntimeBenchmark.cpp
                src/algorithms/TerrainLodView.cpp
                src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
                src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
                src/algorithms/classic_roam/ClassicRoamQueues.cpp
                src/algorithms/classic_roam/ClassicRoamScoring.cpp
                src/algorithms/classic_roam/ClassicRoamState.cpp
                src/algorithms/classic_roam/ClassicRoamTopology.cpp
                src/algorithms/classic_roam/ClassicRoamValidation.cpp
                ${PARALLEL_ROAM_DOD_SOURCES}
                src/benchmark/TerrainLodBenchmark.cpp
                src/benchmark/TerrainLodBenchmarkCommandLine.cpp
                src/benchmark/RoamProbe.cpp
                src/experiment/TerrainLodExperimentCsv.cpp
                src/gui/ImGuiLayer.cpp
                src/platform/Window.cpp
                src/render/Shader.cpp
                src/render/GraphicsBackend.cpp
                src/render/OpenGlGraphicsBackend.cpp
                src/render/OpenGlFrameCapture.cpp
                src/render/TerrainRenderer.cpp
                src/terrain/HeightMap.cpp
                src/terrain/TerrainMeshBuilder.cpp
        )
        message(STATUS "Full Parallel ROAM application target enabled.")
    elseif(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12" AND
           TARGET ParallelROAM::D3D12 AND
           TARGET ParallelROAM::SDL2 AND
           TARGET ParallelROAM::GLM AND
           TARGET ParallelROAM::ImGui AND
           TARGET ParallelROAM::STB)
        set(PARALLEL_ROAM_FULL_APP_AVAILABLE ON)
        list(
            APPEND
            PARALLEL_ROAM_APP_SOURCES
                src/app/Application.cpp
                src/app/ApplicationCommandLine.cpp
                src/app/CameraController.cpp
                src/app/InputState.cpp
                src/app/RuntimeBenchmark.cpp
                src/algorithms/TerrainLodView.cpp
                src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
                src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
                src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
                src/algorithms/classic_roam/ClassicRoamQueues.cpp
                src/algorithms/classic_roam/ClassicRoamScoring.cpp
                src/algorithms/classic_roam/ClassicRoamState.cpp
                src/algorithms/classic_roam/ClassicRoamTopology.cpp
                src/algorithms/classic_roam/ClassicRoamValidation.cpp
                ${PARALLEL_ROAM_DOD_SOURCES}
                src/benchmark/TerrainLodBenchmark.cpp
                src/benchmark/TerrainLodBenchmarkCommandLine.cpp
                src/benchmark/RoamProbe.cpp
                src/experiment/TerrainLodExperimentCsv.cpp
                src/gui/ImGuiLayer.cpp
                src/platform/Window.cpp
                src/render/GraphicsBackend.cpp
                src/render/D3D12AgilitySdkExports.cpp
                src/render/D3D12GraphicsBackend.cpp
                src/render/D3D12FrameCapture.cpp
                src/render/D3D12TerrainRenderer.cpp
                src/terrain/HeightMap.cpp
                src/terrain/TerrainMeshBuilder.cpp
        )
        message(STATUS "Full Parallel ROAM D3D12 application target enabled.")
    else()
        message(STATUS "Full application dependencies are incomplete; building bootstrap target only.")
    endif()

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE)
        list(APPEND PARALLEL_ROAM_APP_SOURCES src/algorithms/TerrainLodAlgorithmRegistry.cpp)
        list(APPEND PARALLEL_ROAM_APP_SOURCES src/benchmark/TransactionalPlatformProbe.cpp)
        list(APPEND PARALLEL_ROAM_APP_SOURCES src/benchmark/TransactionalPlatformReplay.cpp)
        list(APPEND PARALLEL_ROAM_APP_SOURCES ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
            src/benchmark/formal/FormalWorkloadDiscovery.cpp
            src/benchmark/formal/FormalCpuInput.cpp
            src/benchmark/formal/FormalCpuPassBenchmark.cpp
            src/benchmark/formal/FormalTimingCalibration.cpp
            src/benchmark/formal/FormalExperimentRunner.cpp)
    endif()
    if(PARALLEL_ROAM_FULL_APP_AVAILABLE AND TARGET parallel_roam_experiment_infrastructure)
        list(APPEND PARALLEL_ROAM_APP_SOURCES src/app/ExperimentSessionController.cpp src/app/ExperimentCameraSession.cpp src/gui/ExperimentPanel.cpp
            src/benchmark/experiment/ExperimentPlatformReplay.cpp src/benchmark/experiment/ExperimentVisualArtifacts.cpp
            src/benchmark/experiment/ExperimentCameraTools.cpp src/benchmark/experiment/ExperimentLegacyCameraExport.cpp
            src/benchmark/experiment/ExperimentCameraVisualReplay.cpp
            src/benchmark/experiment/ExperimentReplay.cpp src/benchmark/experiment/ExperimentPlatformRun.cpp)
    endif()
    add_executable(parallel_roam src/main.cpp ${PARALLEL_ROAM_APP_SOURCES})
    add_executable(ParallelROAM::app ALIAS parallel_roam)

    target_include_directories(
        parallel_roam
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_BUILD_CONFIG="$<CONFIG>")

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_GRAPHICS_API_OPENGL=1)
    else()
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_GRAPHICS_API_D3D12=1)
    endif()

    target_link_libraries(
        parallel_roam
        PRIVATE
            parallel_roam_project_options
            parallel_roam_project_warnings
            Threads::Threads
    )

    parallel_roam_link_optional_dependencies(parallel_roam)
    if(PARALLEL_ROAM_FULL_APP_AVAILABLE AND TARGET parallel_roam_experiment_infrastructure)
        target_link_libraries(parallel_roam PRIVATE parallel_roam_experiment_infrastructure)
    endif()

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE)
        target_compile_definitions(parallel_roam PRIVATE PARALLEL_ROAM_BUILD_FULL_APP=1)
        if(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME)
            target_link_libraries(parallel_roam PRIVATE parallel_roam_transactional_lod_adapter)
        endif()
    endif()

    if(PARALLEL_ROAM_FULL_APP_AVAILABLE AND PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "D3D12")
        parallel_roam_configure_pinned_d3d12_runtime(parallel_roam PARALLEL_ROAM_DXC_EXECUTABLE)

        set(PARALLEL_ROAM_DX12_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
        set(PARALLEL_ROAM_DX12_VERTEX_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/TerrainVS.cso")
        set(PARALLEL_ROAM_DX12_PIXEL_SHADER "${PARALLEL_ROAM_DX12_SHADER_DIR}/TerrainPS.cso")
        target_compile_definitions(
            parallel_roam
            PRIVATE PARALLEL_ROAM_DX12_SHADER_DIR="${PARALLEL_ROAM_DX12_SHADER_DIR}")
        add_custom_command(
            OUTPUT
                ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_PIXEL_SHADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PARALLEL_ROAM_DX12_SHADER_DIR}"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T vs_6_0 -E VSMain -Fo ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
            COMMAND ${PARALLEL_ROAM_DXC_EXECUTABLE}
                -T ps_6_0 -E PSMain -Fo ${PARALLEL_ROAM_DX12_PIXEL_SHADER}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/dx12/Terrain.hlsl"
            VERBATIM
        )
        add_custom_target(parallel_roam_dx12_shaders
            DEPENDS
                ${PARALLEL_ROAM_DX12_VERTEX_SHADER}
                ${PARALLEL_ROAM_DX12_PIXEL_SHADER})

        add_dependencies(
            parallel_roam
            parallel_roam_dx12_shaders)
        add_custom_command(
            TARGET parallel_roam
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:parallel_roam>/assets/shaders/dx12"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PARALLEL_ROAM_DX12_SHADER_DIR}"
                "$<TARGET_FILE_DIR:parallel_roam>/assets/shaders/dx12"
            COMMENT "Copying compiled D3D12 shaders next to ParallelROAM"
        )
    endif()

    set_target_properties(
        parallel_roam
        PROPERTIES
            OUTPUT_NAME "ParallelROAM"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(
            parallel_roam
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    add_custom_command(
        TARGET parallel_roam
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:parallel_roam>/assets"
        COMMENT "Copying runtime assets next to ParallelROAM"
    )
endif()

if(PARALLEL_ROAM_BUILD_TESTS)
    # 无窗口 CPU 测试只需要数学与图像读取依赖
    parallel_roam_ensure_glm()
    parallel_roam_ensure_stb()
    enable_testing()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(STATUS "PARALLEL_ROAM_BUILD_TESTS is ON, but tests/CMakeLists.txt does not exist yet.")
    endif()
endif()
