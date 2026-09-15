add_executable(parallel_roam_cpu_task_executor_tests CpuTaskExecutorTests.cpp)
target_link_libraries(parallel_roam_cpu_task_executor_tests PRIVATE
    parallel_roam_cpu_execution parallel_roam_project_warnings)
add_test(NAME cpu_task_executor COMMAND parallel_roam_cpu_task_executor_tests)

if(PARALLEL_ROAM_BUILD_PROFILING_TESTS)
    add_executable(parallel_roam_cpu_profiling_tests CpuProfilingTests.cpp
        ../src/tools/profiling/ProfileSession.cpp)
    target_include_directories(parallel_roam_cpu_profiling_tests PRIVATE ../src)
    target_link_libraries(parallel_roam_cpu_profiling_tests PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings Threads::Threads)
    add_test(NAME cpu_profiling_fixture COMMAND parallel_roam_cpu_profiling_tests)
endif()

if(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME)
    add_library(parallel_roam_transactional_adapter_test_support STATIC
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/terrain/HeightMap.cpp ../src/algorithms/TerrainLodView.cpp ../src/tools/PerformanceTimer.cpp)
    target_include_directories(parallel_roam_transactional_adapter_test_support PUBLIC ../src)
    target_link_libraries(parallel_roam_transactional_adapter_test_support PUBLIC
        parallel_roam_project_options ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
    add_executable(parallel_roam_transactional_quality_provenance_probe TransactionalQualityProvenanceProbe.cpp
        ../src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.cpp)
    target_link_libraries(parallel_roam_transactional_quality_provenance_probe PRIVATE
        parallel_roam_transactional_lod_adapter parallel_roam_transactional_adapter_test_support parallel_roam_project_warnings)
    add_executable(parallel_roam_transactional_height_policy_tests TransactionalHeightPolicyTests.cpp)
    target_link_libraries(parallel_roam_transactional_height_policy_tests PRIVATE
        parallel_roam_transactional_lod_adapter parallel_roam_transactional_adapter_test_support parallel_roam_project_warnings)
    add_test(NAME transactional_height_policy COMMAND parallel_roam_transactional_height_policy_tests)
    add_executable(parallel_roam_transactional_adapter_tests TransactionalTerrainLodAlgorithmTests.cpp)
    target_link_libraries(parallel_roam_transactional_adapter_tests PRIVATE
        parallel_roam_transactional_lod_adapter parallel_roam_transactional_adapter_test_support parallel_roam_project_warnings)
    add_test(NAME transactional_adapter COMMAND parallel_roam_transactional_adapter_tests)
    set_tests_properties(transactional_adapter PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 180)
    add_executable(parallel_roam_transactional_render_packet_tests TransactionalRenderPacketTests.cpp)
    target_link_libraries(parallel_roam_transactional_render_packet_tests PRIVATE
        parallel_roam_transactional_lod_adapter parallel_roam_project_warnings)
    add_test(NAME transactional_render_packet COMMAND parallel_roam_transactional_render_packet_tests)
endif()

if(PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD)
    set(transactional_dir "${CMAKE_CURRENT_SOURCE_DIR}/../src/experiment/greedy_transactional_lod")
    # 动态参考只进入研究目标，生产核心不提供 oracle
    add_library(parallel_roam_transactional_dynamic_reference STATIC
        "${transactional_dir}/TransactionalDynamicReference.cpp")
    target_link_libraries(parallel_roam_transactional_dynamic_reference PUBLIC parallel_roam_transactional_lod_core
        PRIVATE parallel_roam_project_warnings)
    if(MSVC)
        target_compile_options(parallel_roam_transactional_dynamic_reference PRIVATE /fp:strict)
    else()
        target_compile_options(parallel_roam_transactional_dynamic_reference PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
    add_executable(parallel_roam_transactional_lod_tests GreedyTransactionalLodTests.cpp
        "${transactional_dir}/TransactionalValidation.cpp")
    add_executable(parallel_roam_transactional_lod_probe GreedyTransactionalLodProbe.cpp
        ../src/tools/profiling/ProfileSession.cpp
        "${transactional_dir}/TransactionalInput.cpp" "${transactional_dir}/TransactionalValidation.cpp"
        "${transactional_dir}/TransactionalScalingProtocol.cpp" "${transactional_dir}/TransactionalQualityReport.cpp"
        ../src/experiment/formal/FormalExperimentCamera.cpp ../src/experiment/formal/FormalExperimentManifest.cpp
        ../src/experiment/ExperimentCsvCodec.cpp ../src/terrain/HeightMap.cpp ../src/algorithms/TerrainLodView.cpp)
    target_link_libraries(parallel_roam_transactional_lod_probe PRIVATE ParallelROAM::STB)
    add_executable(parallel_roam_transactional_lod_family_probe GreedyTransactionalLodFamilyProbe.cpp
        ../src/tools/profiling/ProfileSession.cpp
        "${transactional_dir}/TransactionalInput.cpp" "${transactional_dir}/TransactionalValidation.cpp"
        "${transactional_dir}/TransactionalScalingProtocol.cpp" "${transactional_dir}/TransactionalQualityReport.cpp"
        ${PARALLEL_ROAM_DOD_SOURCES}
        ../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp
        ../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp ../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
        ../src/algorithms/classic_roam/ClassicRoamQueues.cpp ../src/algorithms/classic_roam/ClassicRoamScoring.cpp
        ../src/algorithms/classic_roam/ClassicRoamState.cpp ../src/algorithms/classic_roam/ClassicRoamTopology.cpp
        ../src/algorithms/classic_roam/ClassicRoamValidation.cpp
        ../src/experiment/formal/FormalExperimentCamera.cpp ../src/experiment/formal/FormalExperimentManifest.cpp
        ../src/experiment/ExperimentCsvCodec.cpp ../src/terrain/HeightMap.cpp
        ../src/algorithms/TerrainLodView.cpp ../src/tools/PerformanceTimer.cpp)
    target_link_libraries(parallel_roam_transactional_lod_family_probe PRIVATE parallel_roam_transactional_lod_core
        parallel_roam_project_warnings ParallelROAM::STB Threads::Threads)
    target_include_directories(parallel_roam_transactional_lod_family_probe SYSTEM PRIVATE "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}")
    foreach(transactional_target parallel_roam_transactional_lod_tests parallel_roam_transactional_lod_probe)
        target_sources(${transactional_target} PRIVATE ../src/experiment/roam_materialization/MaterializationExecutor.cpp)
        target_link_libraries(${transactional_target} PRIVATE parallel_roam_transactional_dynamic_reference
            parallel_roam_cpu_execution parallel_roam_project_warnings Threads::Threads)
        target_include_directories(${transactional_target} SYSTEM PRIVATE "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}")
    endforeach()
    add_test(NAME transactional_lod COMMAND parallel_roam_transactional_lod_tests)
    add_executable(parallel_roam_transactional_priority_index_tests TransactionalPriorityIndexTests.cpp)
    target_link_libraries(parallel_roam_transactional_priority_index_tests PRIVATE
        parallel_roam_transactional_lod_core parallel_roam_project_warnings)
    add_test(NAME transactional_priority_index COMMAND parallel_roam_transactional_priority_index_tests)
    add_executable(parallel_roam_transactional_scaling_protocol_tests TransactionalScalingProtocolTests.cpp
        "${transactional_dir}/TransactionalScalingProtocol.cpp"
        ../src/experiment/formal/FormalExperimentCamera.cpp ../src/algorithms/TerrainLodView.cpp)
    target_include_directories(parallel_roam_transactional_scaling_protocol_tests PRIVATE ../src)
    target_link_libraries(parallel_roam_transactional_scaling_protocol_tests PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM)
    add_test(NAME transactional_scaling_protocol COMMAND parallel_roam_transactional_scaling_protocol_tests)
endif()

if(WIN32)
    find_program(PARALLEL_ROAM_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PARALLEL_ROAM_POWERSHELL_EXECUTABLE)
        add_test(
            NAME cpp_comment_coverage
            COMMAND
                ${PARALLEL_ROAM_POWERSHELL_EXECUTABLE}
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_cpp_comment_coverage.ps1"
        )
    else()
        message(WARNING "PowerShell was not found; C++ comment coverage test is disabled.")
    endif()
endif()

add_executable(
    parallel_roam_performance_timer_tests
    PerformanceTimerTests.cpp
    ../src/tools/PerformanceTimer.cpp
)

target_include_directories(
    parallel_roam_performance_timer_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_performance_timer_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        Threads::Threads
)

add_test(NAME performance_timer COMMAND parallel_roam_performance_timer_tests)

add_executable(
    parallel_roam_nested_wedgie_tests
    RoamNestedWedgieTests.cpp
)

target_include_directories(
    parallel_roam_nested_wedgie_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_nested_wedgie_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
)

add_test(NAME roam_nested_wedgie COMMAND parallel_roam_nested_wedgie_tests)

if(NOT TARGET ParallelROAM::GLM)
    message(STATUS "Skipping terrain LOD view tests because GLM is unavailable.")
    return()
endif()

add_executable(
    parallel_roam_screen_projection_tests
    RoamScreenProjectionTests.cpp
    ../src/algorithms/TerrainLodView.cpp
)

target_include_directories(
    parallel_roam_screen_projection_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_screen_projection_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME roam_screen_projection COMMAND parallel_roam_screen_projection_tests)

add_executable(
    parallel_roam_terrain_lod_view_tests
    TerrainLodViewTests.cpp
    ../src/algorithms/TerrainLodView.cpp
)

target_include_directories(
    parallel_roam_terrain_lod_view_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_terrain_lod_view_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME terrain_lod_view COMMAND parallel_roam_terrain_lod_view_tests)

add_executable(
    parallel_roam_experiment_csv_tests
    TerrainLodExperimentCsvTests.cpp
    ../src/experiment/TerrainLodExperimentCsv.cpp
)

target_include_directories(
    parallel_roam_experiment_csv_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_experiment_csv_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME terrain_lod_experiment_csv COMMAND parallel_roam_experiment_csv_tests)

add_executable(
    parallel_roam_terrain_lod_result_validation_tests
    TerrainLodResultValidationTests.cpp
)

target_include_directories(
    parallel_roam_terrain_lod_result_validation_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_terrain_lod_result_validation_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(
    NAME terrain_lod_result_validation
    COMMAND parallel_roam_terrain_lod_result_validation_tests
)

add_executable(
    parallel_roam_application_command_line_tests
    ApplicationCommandLineTests.cpp
    ../src/app/ApplicationCommandLine.cpp
)

target_include_directories(
    parallel_roam_application_command_line_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(
    parallel_roam_application_command_line_tests
    PRIVATE
        parallel_roam_project_options
        parallel_roam_project_warnings
        ParallelROAM::GLM
)

add_test(NAME application_command_line COMMAND parallel_roam_application_command_line_tests)

# 参数测试不链接窗口、渲染器或整个 Benchmark 运行器
add_executable(parallel_roam_benchmark_command_line_tests
    TerrainLodBenchmarkCommandLineTests.cpp
    ../src/benchmark/TerrainLodBenchmarkCommandLine.cpp)
target_include_directories(parallel_roam_benchmark_command_line_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(parallel_roam_benchmark_command_line_tests PRIVATE
    parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM)
add_test(NAME terrain_lod_benchmark_command_line COMMAND parallel_roam_benchmark_command_line_tests)

if(TARGET ParallelROAM::STB)
    # 只读来源诊断不进入默认 CTest，也不链接新算法实现
    add_executable(parallel_roam_greedy_multipass_snapshot_probe GreedyMultipassSnapshotProbe.cpp
        ../src/experiment/greedy_transactional_lod/TransactionalScalingProtocol.cpp
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/benchmark/formal/FormalCpuInput.cpp ../src/algorithms/TerrainLodView.cpp
        ../src/terrain/HeightMap.cpp ../src/tools/PerformanceTimer.cpp)
    target_include_directories(parallel_roam_greedy_multipass_snapshot_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_greedy_multipass_snapshot_probe PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM ParallelROAM::STB Threads::Threads)

    # 原型核心不依赖 DOD，来源桥接和全量验证只进入独立实验入口
    set(materialization_dir "${CMAKE_CURRENT_SOURCE_DIR}/../src/experiment/roam_materialization")
    add_library(parallel_roam_materialization_core STATIC
        "${materialization_dir}/MaterializationHierarchy.cpp"
        "${materialization_dir}/MaterializationState.cpp"
        "${materialization_dir}/MaterializationReference.cpp"
        "${materialization_dir}/MaterializationPatch.cpp")
    target_include_directories(parallel_roam_materialization_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_materialization_core PUBLIC ParallelROAM::GLM
        parallel_roam_project_options PRIVATE parallel_roam_project_warnings)
    add_executable(parallel_roam_RoamMaterialization_tests RoamMaterializationTests.cpp
        "${materialization_dir}/MaterializationExecutor.cpp"
        "${materialization_dir}/MaterializationValidation.cpp"
        "${materialization_dir}/MaterializationDodBridge.cpp"
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/benchmark/formal/FormalCpuInput.cpp ../src/algorithms/TerrainLodView.cpp
        ../src/terrain/HeightMap.cpp ../src/tools/PerformanceTimer.cpp)
    target_link_libraries(parallel_roam_RoamMaterialization_tests PRIVATE
        parallel_roam_materialization_core parallel_roam_project_warnings ParallelROAM::STB Threads::Threads)
    add_test(NAME RoamMaterialization COMMAND parallel_roam_RoamMaterialization_tests)
    set_tests_properties(RoamMaterialization PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." TIMEOUT 120)
    add_executable(parallel_roam_materialization_probe RoamMaterializationProbe.cpp
        "${materialization_dir}/MaterializationExecutor.cpp"
        "${materialization_dir}/MaterializationValidation.cpp" "${materialization_dir}/MaterializationDodBridge.cpp"
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/benchmark/formal/FormalCpuInput.cpp ../src/algorithms/TerrainLodView.cpp
        ../src/terrain/HeightMap.cpp ../src/tools/PerformanceTimer.cpp)
    target_link_libraries(parallel_roam_materialization_probe PRIVATE parallel_roam_materialization_core
        parallel_roam_project_warnings ParallelROAM::STB Threads::Threads)

    # 平台实际 float 输出的离线评价，不链接进正常图形应用
    add_executable(parallel_roam_transactional_platform_quality_probe TransactionalPlatformQualityProbe.cpp
        ../src/experiment/mesh_quality/MeshQualityEvaluator.cpp
        ../src/terrain/HeightMap.cpp ../src/terrain/TerrainMeshBuilder.cpp)
    target_include_directories(parallel_roam_transactional_platform_quality_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_transactional_platform_quality_probe PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM ParallelROAM::STB)

    add_executable(parallel_roam_MeshQualityEvaluator_tests MeshQualityEvaluatorTests.cpp
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/experiment/mesh_quality/MeshQualityEvaluator.cpp
        ../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp
        ../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp
        ../src/algorithms/classic_roam/ClassicRoamQueues.cpp
        ../src/algorithms/classic_roam/ClassicRoamScoring.cpp
        ../src/algorithms/classic_roam/ClassicRoamState.cpp
        ../src/algorithms/classic_roam/ClassicRoamTopology.cpp
        ../src/algorithms/classic_roam/ClassicRoamValidation.cpp
        ../src/algorithms/TerrainLodView.cpp ../src/terrain/HeightMap.cpp
        ../src/terrain/TerrainMeshBuilder.cpp ../src/tools/PerformanceTimer.cpp)
    target_include_directories(parallel_roam_MeshQualityEvaluator_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_MeshQualityEvaluator_tests PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings
        ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
    add_test(NAME MeshQualityEvaluator COMMAND parallel_roam_MeshQualityEvaluator_tests)
    set_tests_properties(MeshQualityEvaluator PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." TIMEOUT 120)

    add_executable(parallel_roam_runtime_performance_probe TerrainLodRuntimePerformanceProbe.cpp
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/algorithms/TerrainLodView.cpp ../src/terrain/HeightMap.cpp
        ../src/tools/PerformanceTimer.cpp ../src/experiment/TerrainLodExperimentCsv.cpp)
    target_include_directories(parallel_roam_runtime_performance_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_runtime_performance_probe PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings
        ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
    add_executable(parallel_roam_native_materialization_probe DataOrientedRoamNativeMaterializationProbe.cpp
        ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
        ../src/algorithms/TerrainLodView.cpp ../src/terrain/HeightMap.cpp ../src/tools/PerformanceTimer.cpp)
    target_include_directories(parallel_roam_native_materialization_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_native_materialization_probe PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings
        ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
    foreach(discovery_test IN ITEMS DataOrientedRoamPassExecution DataOrientedRoamWorkloadProbe
        DataOrientedRoamPassExperiment FormalCpuPair DataOrientedRoamSplitPrefix FormalSplitTopologyEquivalence OrderingGate
        DataOrientedRoamNativeTarget DataOrientedRoamNativePlanning DataOrientedRoamNativePlanner DataOrientedRoamCandidateBirth)
        add_executable(parallel_roam_${discovery_test}_tests ${discovery_test}Tests.cpp
            ${PARALLEL_ROAM_DOD_SOURCES} ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES} ../src/algorithms/TerrainLodView.cpp
            ../src/terrain/HeightMap.cpp ../src/tools/PerformanceTimer.cpp)
        target_include_directories(parallel_roam_${discovery_test}_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
        target_link_libraries(parallel_roam_${discovery_test}_tests PRIVATE
            parallel_roam_project_options parallel_roam_project_warnings
            ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
        add_test(NAME ${discovery_test} COMMAND parallel_roam_${discovery_test}_tests)
        set_tests_properties(${discovery_test} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." TIMEOUT 120)
    endforeach()
    target_sources(parallel_roam_DataOrientedRoamPassExecution_tests PRIVATE
        ../src/benchmark/formal/FormalWorkloadDiscovery.cpp
        ../src/benchmark/formal/FormalCpuInput.cpp)
    target_sources(parallel_roam_FormalCpuPair_tests PRIVATE
        ../src/benchmark/formal/FormalCpuInput.cpp ../src/benchmark/formal/FormalTimingCalibration.cpp)
    target_sources(parallel_roam_FormalSplitTopologyEquivalence_tests PRIVATE
        ../src/benchmark/formal/FormalCpuInput.cpp)
    target_sources(parallel_roam_OrderingGate_tests PRIVATE
        ../src/benchmark/formal/FormalCpuInput.cpp)
    add_executable(parallel_roam_dod_pass_policy_tests
        DataOrientedRoamPassPolicyTests.cpp
        ${PARALLEL_ROAM_DOD_SOURCES}
        ../src/algorithms/TerrainLodView.cpp
        ../src/terrain/HeightMap.cpp
        ../src/tools/PerformanceTimer.cpp)
    target_include_directories(parallel_roam_dod_pass_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_dod_pass_policy_tests PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings
        ParallelROAM::GLM ParallelROAM::STB Threads::Threads)
    add_test(NAME dod_pass_policy COMMAND parallel_roam_dod_pass_policy_tests)
    set_tests_properties(dod_pass_policy PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." TIMEOUT 60)
endif()

if(TARGET ParallelROAM::STB)
    foreach(input_test IN ITEMS ExperimentManifest ExperimentCamera ExperimentTargetSelector)
        add_executable(parallel_roam_${input_test}_tests
            ${input_test}Tests.cpp ${PARALLEL_ROAM_FORMAL_INPUT_SOURCES}
            ../src/algorithms/TerrainLodView.cpp ../src/terrain/HeightMap.cpp)
        target_include_directories(parallel_roam_${input_test}_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
        target_link_libraries(parallel_roam_${input_test}_tests PRIVATE
            parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM ParallelROAM::STB)
    endforeach()
    add_test(NAME formal_experiment_manifest COMMAND parallel_roam_ExperimentManifest_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/formal-manifest")
    add_test(NAME formal_experiment_camera COMMAND parallel_roam_ExperimentCamera_tests "${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME formal_experiment_target_selector COMMAND parallel_roam_ExperimentTargetSelector_tests)
    add_executable(parallel_roam_formal_command_line_tests FormalExperimentCommandLineTests.cpp
        ../src/benchmark/TerrainLodBenchmarkCommandLine.cpp)
    target_include_directories(parallel_roam_formal_command_line_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(parallel_roam_formal_command_line_tests PRIVATE
        parallel_roam_project_options parallel_roam_project_warnings ParallelROAM::GLM)
    add_test(NAME formal_experiment_command_line COMMAND parallel_roam_formal_command_line_tests)
endif()

if(PARALLEL_ROAM_FULL_APP_AVAILABLE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME runtime_performance_analysis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_runtime_performance_analysis.py")
    add_test(NAME runtime_performance_runner COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_runtime_performance_runner.py"
        --application "$<TARGET_FILE:parallel_roam>" --probe "$<TARGET_FILE:parallel_roam_runtime_performance_probe>")
    set_tests_properties(runtime_performance_analysis runtime_performance_runner PROPERTIES TIMEOUT 120)
    cmake_path(GET CMAKE_BINARY_DIR FILENAME cpu_pilot_build_preset)
    add_test(NAME cpu_pilot_preparation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_cpu_pilot_preparation.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/.." --executable "$<TARGET_FILE:parallel_roam>"
        --preset "${cpu_pilot_build_preset}" --backend "${PARALLEL_ROAM_GRAPHICS_API}"
        --configuration "$<CONFIG>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/cpu-pilot-preparation")
    set_tests_properties(cpu_pilot_preparation PROPERTIES TIMEOUT 180)
    add_test(NAME cpu_workload_discovery COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_cpu_workload_discovery.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/.." --executable "$<TARGET_FILE:parallel_roam>"
        --replay-verifier "$<TARGET_FILE:parallel_roam_DataOrientedRoamPassExecution_tests>"
        --preset "${cpu_pilot_build_preset}" --output "${CMAKE_CURRENT_BINARY_DIR}/cpu-workload-discovery")
    set_tests_properties(cpu_workload_discovery PROPERTIES TIMEOUT 900)
    add_test(NAME cpu_pilot_pairing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_cpu_pilot_pairing.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/.." --executable "$<TARGET_FILE:parallel_roam>"
        --preset "${cpu_pilot_build_preset}" --output "${CMAKE_CURRENT_BINARY_DIR}/cpu-pilot-pairing")
    set_tests_properties(cpu_pilot_pairing PROPERTIES TIMEOUT 900)
    add_test(NAME pass_crossover_analysis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_pass_crossover_analysis.py")
    set_tests_properties(pass_crossover_analysis PROPERTIES TIMEOUT 60)
    add_test(NAME terrain_lod_benchmark_policy
        COMMAND "${CMAKE_COMMAND}"
            "-DBENCHMARK=$<TARGET_FILE:parallel_roam>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/benchmark-policy"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/TerrainLodBenchmarkPolicyTests.cmake")
    set_tests_properties(terrain_lod_benchmark_policy PROPERTIES TIMEOUT 180)
endif()

if(TARGET parallel_roam)
    add_test(
        NAME roam_budget_reentry_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile budget-reentry
    )
    add_test(
        NAME roam_budget_reentry_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile budget-reentry
    )
    add_test(
        NAME roam_split_queue_view_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile smoke
    )
    add_test(
        NAME roam_incremental_mesh_emit_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile incremental-emit
    )
    add_test(
        NAME roam_incremental_mesh_emit_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile incremental-emit
    )
    add_test(
        NAME roam_pass_trace_replay_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile pass-trace-replay
    )
    add_test(
        NAME roam_pass_trace_replay_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile pass-trace-replay
    )
    add_test(
        NAME roam_pass_policy_replay_classic
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm classic
            --profile pass-policy-replay
    )
    add_test(
        NAME roam_pass_policy_replay_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile pass-policy-replay
    )
    add_test(
        NAME roam_topology_pair_replay_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile topology-pair-replay
    )
    add_test(
        NAME roam_classic_dod_result_contract
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm all
            --profile classic-dod-contract
    )
    add_test(
        NAME roam_pass_crossover_replay_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile pass-crossover-replay
            --pass-warmups 0
            --pass-repeats 1
            --pass-targets 1
            --csv "${CMAKE_CURRENT_BINARY_DIR}/pass-crossover-replay.csv"
    )
    add_test(
        NAME roam_pass_crossover_stress_replay_dod
        COMMAND
            $<TARGET_FILE:parallel_roam>
            --benchmark
            --algorithm dod
            --profile pass-crossover-stress-replay
            --pass-warmups 0
            --pass-repeats 1
            --pass-targets 1
            --csv "${CMAKE_CURRENT_BINARY_DIR}/pass-crossover-stress-replay.csv"
    )
    set_tests_properties(
        roam_budget_reentry_classic
        roam_budget_reentry_dod
        roam_split_queue_view_dod
        roam_incremental_mesh_emit_classic
        roam_incremental_mesh_emit_dod
        roam_pass_trace_replay_classic
        roam_pass_trace_replay_dod
        roam_pass_policy_replay_classic
        roam_pass_policy_replay_dod
        roam_topology_pair_replay_dod
        roam_classic_dod_result_contract
        roam_pass_crossover_replay_dod
        roam_pass_crossover_stress_replay_dod
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endif()

if(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME)
    add_executable(parallel_roam_transactional_command_line_tests ApplicationCommandLineTests.cpp
        ../src/app/ApplicationCommandLine.cpp)
    target_link_libraries(parallel_roam_transactional_command_line_tests PRIVATE
        parallel_roam_project_options ParallelROAM::GLM)
    target_include_directories(parallel_roam_transactional_command_line_tests PRIVATE ../src)
    target_compile_definitions(parallel_roam_transactional_command_line_tests PRIVATE PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME=1)
    add_test(NAME transactional_command_line COMMAND parallel_roam_transactional_command_line_tests)
endif()

if(TARGET parallel_roam_experiment_infrastructure AND TARGET ParallelROAM::STB)
    add_executable(parallel_roam_experiment_infrastructure_tests ExperimentInfrastructureTests.cpp
        ../src/terrain/HeightMap.cpp ../src/experiment/ExperimentCsvCodec.cpp)
    target_include_directories(parallel_roam_experiment_infrastructure_tests SYSTEM PRIVATE "${PARALLEL_ROAM_EXPERIMENT_BOOST_INCLUDE}")
    target_link_libraries(parallel_roam_experiment_infrastructure_tests PRIVATE parallel_roam_experiment_infrastructure
        ParallelROAM::STB parallel_roam_project_warnings)
endif()
