# 显式实验入口不改变默认应用依赖；仅开启时复用现有Boost头文件
option(PARALLEL_ROAM_ENABLE_EXPERIMENT_INFRASTRUCTURE "Build experiment asset and replay infrastructure" OFF)
if(NOT PARALLEL_ROAM_ENABLE_EXPERIMENT_INFRASTRUCTURE)
    return()
endif()
find_path(PARALLEL_ROAM_EXPERIMENT_BOOST_INCLUDE boost/property_tree/json_parser.hpp
    HINTS "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}")
if(NOT PARALLEL_ROAM_EXPERIMENT_BOOST_INCLUDE)
    message(FATAL_ERROR "Experiment infrastructure requires Boost.PropertyTree headers.")
endif()
add_library(parallel_roam_experiment_infrastructure STATIC
    "${PROJECT_SOURCE_DIR}/src/experiment/infrastructure/ExperimentCase.cpp"
    "${PROJECT_SOURCE_DIR}/src/experiment/infrastructure/TerrainAssetCatalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/experiment/infrastructure/ExperimentRecords.cpp")
target_include_directories(parallel_roam_experiment_infrastructure PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(parallel_roam_experiment_infrastructure SYSTEM PRIVATE "${PARALLEL_ROAM_EXPERIMENT_BOOST_INCLUDE}")
target_link_libraries(parallel_roam_experiment_infrastructure PUBLIC parallel_roam_project_options ParallelROAM::GLM
    PRIVATE parallel_roam_project_warnings)
target_compile_definitions(parallel_roam_experiment_infrastructure PUBLIC PARALLEL_ROAM_EXPERIMENT_INFRASTRUCTURE=1)
