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
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endif()
