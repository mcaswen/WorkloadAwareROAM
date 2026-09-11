if(PARALLEL_ROAM_BUILD_CPU_CBT_PROTOTYPE)
    # 无窗口原型独立准备纯 CPU 依赖，不触发应用的图形依赖初始化
    parallel_roam_ensure_glm()
    parallel_roam_ensure_stb()
    if(NOT TARGET ParallelROAM::GLM OR NOT TARGET ParallelROAM::STB)
        message(FATAL_ERROR "CPU CBT requires the existing GLM and STB dependencies.")
    endif()
    set(cpu_cbt_reference_root "${PROJECT_SOURCE_DIR}/third_party/RoamTesting/src")
    set(cpu_cbt_reference_sources)
    foreach(component IN ITEMS CbtOccupancyTree CbtBisectorTopology CbtClassification CbtSplitPlanner
            CbtBisectCommit CbtSimplifyCommit CbtTerrainGeometry)
        set(source "${cpu_cbt_reference_root}/algorithms/cbt_2024/${component}.cpp")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "CPU CBT reference source is missing: ${source}")
        endif()
        list(APPEND cpu_cbt_reference_sources "${source}")
    endforeach()
    add_library(parallel_roam_cpu_cbt_reference STATIC ${cpu_cbt_reference_sources})
    target_include_directories(parallel_roam_cpu_cbt_reference PUBLIC
        "${PROJECT_SOURCE_DIR}/src" "${cpu_cbt_reference_root}")
    target_link_libraries(parallel_roam_cpu_cbt_reference PUBLIC parallel_roam_project_options
        ParallelROAM::GLM PRIVATE parallel_roam_project_warnings)
    add_library(parallel_roam_cpu_cbt STATIC
        ../src/algorithms/cpu_cbt/CpuCbtState.cpp ../src/algorithms/cpu_cbt/CpuCbtUpdate.cpp
        ../src/algorithms/cpu_cbt/CpuCbtBisect.cpp
        ../src/algorithms/cpu_cbt/CpuCbtMesh.cpp ../src/terrain/HeightMap.cpp
        ../src/algorithms/TerrainLodView.cpp)
    target_link_libraries(parallel_roam_cpu_cbt PUBLIC parallel_roam_cpu_cbt_reference
        PRIVATE ParallelROAM::STB parallel_roam_project_warnings)
    add_executable(parallel_roam_cpu_cbt_tests CpuCbtTests.cpp)
    add_executable(parallel_roam_cpu_cbt_probe CpuCbtProbe.cpp ../src/experiment/ExperimentCsvCodec.cpp)
    foreach(target IN ITEMS parallel_roam_cpu_cbt_tests parallel_roam_cpu_cbt_probe)
        target_sources(${target} PRIVATE ../src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp)
        target_link_libraries(${target} PRIVATE parallel_roam_cpu_cbt parallel_roam_project_warnings Threads::Threads)
    endforeach()
    add_test(NAME CpuCbt COMMAND parallel_roam_cpu_cbt_tests)
    set_tests_properties(CpuCbt PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120)
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
    foreach(discovery_test IN ITEMS DataOrientedRoamPassExecution DataOrientedRoamWorkloadProbe
        DataOrientedRoamPassExperiment FormalCpuPair DataOrientedRoamSplitPrefix FormalSplitTopologyEquivalence OrderingGate)
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
