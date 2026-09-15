include_guard(GLOBAL)

option(PARALLEL_ROAM_ENABLE_CBT_2024 "Build the imported CBT 2024 reference" OFF)

function(parallel_roam_prepare_cbt_core)
    if(NOT PARALLEL_ROAM_ENABLE_CBT_2024 OR TARGET parallel_roam_cbt_core)
        return()
    endif()

    parallel_roam_ensure_glm()
    parallel_roam_ensure_stb()
    set(cbt_source_dir "${PROJECT_SOURCE_DIR}/src/algorithms/cbt_2024")
    add_library(parallel_roam_cbt_core STATIC
        "${cbt_source_dir}/CbtOccupancyTree.cpp"
        "${cbt_source_dir}/CbtBisectorTopology.cpp"
        "${cbt_source_dir}/CbtClassification.cpp"
        "${cbt_source_dir}/CbtSplitPlanner.cpp"
        "${cbt_source_dir}/CbtBisectCommit.cpp"
        "${cbt_source_dir}/CbtSimplifyCommit.cpp"
        "${cbt_source_dir}/CbtTerrainGeometry.cpp"
        "${PROJECT_SOURCE_DIR}/src/algorithms/TerrainLodView.cpp"
        "${PROJECT_SOURCE_DIR}/src/terrain/HeightMap.cpp")
    target_include_directories(parallel_roam_cbt_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(parallel_roam_cbt_core
        PUBLIC parallel_roam_project_options ParallelROAM::GLM ParallelROAM::STB
        PRIVATE parallel_roam_project_warnings)
endfunction()

function(parallel_roam_add_cbt_tests)
    if(NOT PARALLEL_ROAM_ENABLE_CBT_2024)
        return()
    endif()

    parallel_roam_prepare_cbt_core()
    # 每个入口保留来源夹具，核心源码只在共享库中编译一次
    foreach(component IN ITEMS OccupancyTree BisectorTopology Classification
            SplitPlanner BisectCommit SimplifyCommit TerrainGeometry IntegrationContract)
        set(test_target "parallel_roam_cbt_${component}_tests")
        add_executable(${test_target} "${PROJECT_SOURCE_DIR}/tests/Cbt${component}Tests.cpp")
        target_link_libraries(${test_target} PRIVATE
            parallel_roam_cbt_core parallel_roam_project_warnings)
        add_test(NAME "cbt_${component}" COMMAND ${test_target})
        set_tests_properties("cbt_${component}" PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endforeach()
endfunction()

function(parallel_roam_add_cbt_shader source entry output_name profile capacity)
    set(shader_path "${PARALLEL_ROAM_DX12_SHADER_DIR}/${output_name}.cso")
    set(defines)
    if(NOT "${capacity}" STREQUAL "")
        list(APPEND defines -D "CBT_CAPACITY=${capacity}")
    endif()
    add_custom_command(
        OUTPUT "${shader_path}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PARALLEL_ROAM_DX12_SHADER_DIR}"
        COMMAND "${PARALLEL_ROAM_DXC_EXECUTABLE}"
            -T "${profile}" -E "${entry}" ${defines}
            -I "${PROJECT_SOURCE_DIR}/assets/shaders/dx12/cbt"
            -I "${PROJECT_SOURCE_DIR}/src/algorithms"
            -I "${PROJECT_SOURCE_DIR}/src/algorithms/cbt_2024"
            -Fo "${shader_path}" "${PROJECT_SOURCE_DIR}/assets/shaders/dx12/${source}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/assets/shaders/dx12/${source}"
            "${PROJECT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtOccupancyTree.hlsli"
            "${PROJECT_SOURCE_DIR}/assets/shaders/dx12/cbt/CbtGpuAbi.hlsli"
            "${PROJECT_SOURCE_DIR}/src/algorithms/cbt_2024/CbtGpuAbi.shared.h"
            "${PROJECT_SOURCE_DIR}/src/algorithms/cbt_2024/CbtDebugVisualization.shared.h"
        VERBATIM)
    set(PARALLEL_ROAM_CBT_SHADER_OUTPUTS
        ${PARALLEL_ROAM_CBT_SHADER_OUTPUTS} "${shader_path}" PARENT_SCOPE)
endfunction()

function(parallel_roam_prepare_cbt_shaders target)
    if(NOT PARALLEL_ROAM_ENABLE_CBT_2024)
        return()
    endif()

    set(PARALLEL_ROAM_CBT_SHADER_OUTPUTS)
    parallel_roam_add_cbt_shader(CbtProceduralTerrain.hlsl VSCbtProcedural
        CbtProceduralTerrainVS vs_6_6 "")
    foreach(entry IN ITEMS Indexation PrepareIndirect BuildActiveGeometry
            BuildModifiedGeometry ValidateGeometryG)
        parallel_roam_add_cbt_shader(cbt/CbtBootstrap.hlsl "CS${entry}"
            "CbtBootstrap${entry}" cs_6_6 "")
    endforeach()

    set(capacity_names 128K 256K 512K 1M)
    set(capacity_values 131072 262144 524288 1048576)
    foreach(capacity_index RANGE 0 3)
        list(GET capacity_names ${capacity_index} capacity_name)
        list(GET capacity_values ${capacity_index} capacity_value)
        foreach(entry IN ITEMS Clear ApplyUpdates ReducePre ReduceFirst
                ReduceSecond DecodeOccupied DecodeFree)
            parallel_roam_add_cbt_shader(cbt/CbtOccupancyTreeTests.hlsl "CS${entry}"
                "CbtOcbt${capacity_name}${entry}" cs_6_6 "${capacity_value}")
        endforeach()
        foreach(entry IN ITEMS ResetE0 Classify PrepareClassificationIndirect
                SplitE2 PrepareAllocationIndirect AllocateE2 BisectE3
                PreparePropagationIndirectE3 PropagateBisectE3 PrepareSimplifyF
                PrepareSimplifyIndirectF SimplifyF PrepareSimplifyPropagationIndirectF
                PropagateSimplifyF ReducePre ReduceFirst ReduceSecond ValidateF)
            parallel_roam_add_cbt_shader(cbt/CbtTopologyE0.hlsl "CS${entry}"
                "CbtTopology${capacity_name}${entry}" cs_6_6 "${capacity_value}")
        endforeach()
    endforeach()
    add_custom_target(parallel_roam_cbt_shaders DEPENDS ${PARALLEL_ROAM_CBT_SHADER_OUTPUTS})
    add_dependencies(${target} parallel_roam_cbt_shaders)
endfunction()

function(parallel_roam_attach_cbt_runtime target)
    if(NOT PARALLEL_ROAM_ENABLE_CBT_2024)
        return()
    endif()

    parallel_roam_prepare_cbt_core()
    set(cbt_dir "${PROJECT_SOURCE_DIR}/src/algorithms/cbt_2024")
    target_sources(${target} PRIVATE
        "${cbt_dir}/Cbt2024Support.cpp"
        "${cbt_dir}/d3d12/D3D12CbtTerrainLodAlgorithm.cpp"
        "${cbt_dir}/d3d12/D3D12CbtGpuState.cpp"
        "${cbt_dir}/d3d12/D3D12CbtOccupancyTree.cpp"
        "${cbt_dir}/d3d12/D3D12CbtFramePipeline.cpp"
        "${cbt_dir}/d3d12/D3D12CbtGeometryPipeline.cpp"
        "${cbt_dir}/d3d12/D3D12CbtDiagnostics.cpp"
        "${PROJECT_SOURCE_DIR}/src/render/D3D12CbtRenderPass.cpp")
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/benchmark/cbt_2024/CbtPlatformCheck.cpp")
    target_link_libraries(${target} PRIVATE parallel_roam_cbt_core)
    # 同一构建内的 CLI/GUI 库必须看到与工厂一致的可用性
    target_compile_definitions(parallel_roam_project_options INTERFACE PARALLEL_ROAM_CBT_2024_RUNTIME=1)
endfunction()
