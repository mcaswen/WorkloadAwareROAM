include(FetchContent)

function(parallel_roam_make_interface_alias target alias)
    if(NOT TARGET ${alias})
        add_library(${target} INTERFACE)
        add_library(${alias} ALIAS ${target})
    endif()
endfunction()

function(parallel_roam_ensure_opengl)
    if(TARGET ParallelROAM::OpenGL OR NOT PARALLEL_ROAM_WITH_OPENGL)
        return()
    endif()

    find_package(OpenGL QUIET)

    if(OpenGL_FOUND)
        parallel_roam_make_interface_alias(parallel_roam_opengl ParallelROAM::OpenGL)
        target_link_libraries(parallel_roam_opengl INTERFACE OpenGL::GL)
        message(STATUS "OpenGL found and linked.")
    else()
        message(STATUS "OpenGL not found. Install an OpenGL SDK/driver package for the target platform.")
    endif()
endfunction()

function(parallel_roam_ensure_sdl2)
    if(TARGET ParallelROAM::SDL2 OR NOT PARALLEL_ROAM_WITH_SDL2)
        return()
    endif()

    find_package(SDL2 CONFIG QUIET)

    if(TARGET SDL2::SDL2 OR TARGET SDL2::SDL2-static)
        parallel_roam_make_interface_alias(parallel_roam_sdl2 ParallelROAM::SDL2)

        if(TARGET SDL2::SDL2main)
            target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2main)
        endif()

        if(TARGET SDL2::SDL2)
            target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2)
            message(STATUS "SDL2 found through CMake config and linked.")
        elseif(TARGET SDL2::SDL2-static)
            target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2-static)
            message(STATUS "SDL2 static target found through CMake config and linked.")
        endif()

        return()
    endif()

    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(PKG_SDL2 QUIET sdl2)
    endif()

    if(PKG_SDL2_FOUND)
        parallel_roam_make_interface_alias(parallel_roam_sdl2 ParallelROAM::SDL2)
        target_include_directories(parallel_roam_sdl2 INTERFACE ${PKG_SDL2_INCLUDE_DIRS})
        target_link_libraries(parallel_roam_sdl2 INTERFACE ${PKG_SDL2_LINK_LIBRARIES})
        target_compile_options(parallel_roam_sdl2 INTERFACE ${PKG_SDL2_CFLAGS_OTHER})
        message(STATUS "SDL2 found through pkg-config and linked.")
        return()
    endif()

    set(PARALLEL_ROAM_LOCAL_SDL2_DIR "${PROJECT_SOURCE_DIR}/third_party/SDL2")
    if(EXISTS "${PARALLEL_ROAM_LOCAL_SDL2_DIR}/CMakeLists.txt")
        set(SDL_SHARED OFF CACHE BOOL "Build SDL2 as a shared library." FORCE)
        set(SDL_STATIC ON CACHE BOOL "Build SDL2 as a static library." FORCE)
        set(SDL_TEST OFF CACHE BOOL "Build SDL2 tests." FORCE)
        add_subdirectory("${PARALLEL_ROAM_LOCAL_SDL2_DIR}" "${CMAKE_BINARY_DIR}/third_party/SDL2" EXCLUDE_FROM_ALL)

        if(TARGET SDL2::SDL2-static OR TARGET SDL2-static)
            parallel_roam_make_interface_alias(parallel_roam_sdl2 ParallelROAM::SDL2)

            if(TARGET SDL2::SDL2main)
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2main)
            endif()

            if(TARGET SDL2::SDL2-static)
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2-static)
            else()
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2-static)
            endif()

            message(STATUS "SDL2 local third_party source found and linked.")
            return()
        endif()
    endif()

    if(PARALLEL_ROAM_FETCH_MISSING_DEPS)
        set(SDL_SHARED OFF CACHE BOOL "Build SDL2 as a shared library." FORCE)
        set(SDL_STATIC ON CACHE BOOL "Build SDL2 as a static library." FORCE)
        set(SDL_TEST OFF CACHE BOOL "Build SDL2 tests." FORCE)

        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG ${PARALLEL_ROAM_SDL2_FETCH_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL2)

        if(TARGET SDL2::SDL2-static OR TARGET SDL2-static)
            parallel_roam_make_interface_alias(parallel_roam_sdl2 ParallelROAM::SDL2)

            if(TARGET SDL2::SDL2main)
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2main)
            endif()

            if(TARGET SDL2::SDL2-static)
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2::SDL2-static)
            else()
                target_link_libraries(parallel_roam_sdl2 INTERFACE SDL2-static)
            endif()

            message(STATUS "SDL2 fetched with FetchContent and linked.")
            return()
        endif()
    endif()

    message(STATUS "SDL2 not found. Install SDL2, use vcpkg, or configure with -DPARALLEL_ROAM_FETCH_MISSING_DEPS=ON.")
endfunction()

function(parallel_roam_ensure_glm)
    if(TARGET ParallelROAM::GLM OR NOT PARALLEL_ROAM_WITH_GLM)
        return()
    endif()

    find_package(glm CONFIG QUIET)

    if(TARGET glm::glm)
        parallel_roam_make_interface_alias(parallel_roam_glm ParallelROAM::GLM)
        target_link_libraries(parallel_roam_glm INTERFACE glm::glm)
        message(STATUS "GLM found and linked.")
        return()
    endif()

    set(PARALLEL_ROAM_LOCAL_GLM_DIR "${PROJECT_SOURCE_DIR}/third_party/glm")
    if(EXISTS "${PARALLEL_ROAM_LOCAL_GLM_DIR}/CMakeLists.txt")
        set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Do not build GLM library target." FORCE)
        set(GLM_BUILD_TESTS OFF CACHE BOOL "Do not build GLM tests." FORCE)
        add_subdirectory("${PARALLEL_ROAM_LOCAL_GLM_DIR}" "${CMAKE_BINARY_DIR}/third_party/glm" EXCLUDE_FROM_ALL)

        if(TARGET glm::glm)
            parallel_roam_make_interface_alias(parallel_roam_glm ParallelROAM::GLM)
            target_link_libraries(parallel_roam_glm INTERFACE glm::glm)
            message(STATUS "GLM local third_party source found and linked.")
            return()
        endif()
    endif()

    if(PARALLEL_ROAM_FETCH_MISSING_DEPS)
        set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Do not build GLM library target." FORCE)
        set(GLM_BUILD_TESTS OFF CACHE BOOL "Do not build GLM tests." FORCE)

        FetchContent_Declare(
            glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG ${PARALLEL_ROAM_GLM_FETCH_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glm)

        if(TARGET glm::glm)
            parallel_roam_make_interface_alias(parallel_roam_glm ParallelROAM::GLM)
            target_link_libraries(parallel_roam_glm INTERFACE glm::glm)
            message(STATUS "GLM fetched with FetchContent and linked.")
            return()
        endif()
    endif()

    message(STATUS "GLM not found. Install GLM, use vcpkg, or configure with -DPARALLEL_ROAM_FETCH_MISSING_DEPS=ON.")
endfunction()

function(parallel_roam_ensure_glad)
    if(TARGET ParallelROAM::GLAD OR NOT PARALLEL_ROAM_WITH_GLAD)
        return()
    endif()

    set(PARALLEL_ROAM_LOCAL_GLAD_DIR "${PROJECT_SOURCE_DIR}/third_party/glad")
    set(PARALLEL_ROAM_LOCAL_GLAD_SOURCE "${PARALLEL_ROAM_LOCAL_GLAD_DIR}/src/gl.c")
    set(PARALLEL_ROAM_LOCAL_GLAD_HEADER "${PARALLEL_ROAM_LOCAL_GLAD_DIR}/include/glad/gl.h")

    if(EXISTS "${PARALLEL_ROAM_LOCAL_GLAD_SOURCE}" AND EXISTS "${PARALLEL_ROAM_LOCAL_GLAD_HEADER}")
        add_library(parallel_roam_glad_loader STATIC "${PARALLEL_ROAM_LOCAL_GLAD_SOURCE}")
        target_include_directories(parallel_roam_glad_loader PUBLIC "${PARALLEL_ROAM_LOCAL_GLAD_DIR}/include")
        target_link_libraries(parallel_roam_glad_loader PUBLIC ${CMAKE_DL_LIBS})

        parallel_roam_make_interface_alias(parallel_roam_glad ParallelROAM::GLAD)
        target_link_libraries(parallel_roam_glad INTERFACE parallel_roam_glad_loader)
        message(STATUS "GLAD local OpenGL core 4.3 loader found and linked.")
        return()
    endif()

    find_package(glad CONFIG QUIET)

    if(TARGET glad::glad)
        parallel_roam_make_interface_alias(parallel_roam_glad ParallelROAM::GLAD)
        target_link_libraries(parallel_roam_glad INTERFACE glad::glad)
        message(STATUS "GLAD found and linked.")
        return()
    endif()

    if(PARALLEL_ROAM_FETCH_MISSING_DEPS)
        find_package(Python3 COMPONENTS Interpreter QUIET)

        if(NOT Python3_Interpreter_FOUND)
            message(STATUS "GLAD FetchContent requires Python3 for code generation; Python3 was not found.")
        else()
            execute_process(
                COMMAND ${Python3_EXECUTABLE} -c "import jinja2"
                RESULT_VARIABLE PARALLEL_ROAM_GLAD_HAS_JINJA2
                OUTPUT_QUIET
                ERROR_QUIET
            )

            if(NOT PARALLEL_ROAM_GLAD_HAS_JINJA2 EQUAL 0)
                message(STATUS "GLAD FetchContent requires the Python package 'jinja2'. Skipping GLAD generation; use vcpkg or provide a pre-generated GLAD package.")
                return()
            endif()

            FetchContent_Declare(
                glad
                GIT_REPOSITORY https://github.com/Dav1dde/glad.git
                GIT_TAG ${PARALLEL_ROAM_GLAD_FETCH_TAG}
                GIT_SHALLOW TRUE
            )
            FetchContent_MakeAvailable(glad)

            set(GLAD_CMAKE_DIR "${glad_SOURCE_DIR}/cmake" CACHE STRING "Directory containing glad CMake helpers." FORCE)
            set(GLAD_SOURCES_DIR "${glad_SOURCE_DIR}" CACHE STRING "Directory containing glad Python sources." FORCE)
            include(${glad_SOURCE_DIR}/cmake/GladConfig.cmake)

            if(COMMAND glad_add_library)
                glad_add_library(parallel_roam_glad_loader STATIC REPRODUCIBLE LOADER API gl:core=4.3)
                parallel_roam_make_interface_alias(parallel_roam_glad ParallelROAM::GLAD)
                target_link_libraries(parallel_roam_glad INTERFACE parallel_roam_glad_loader)
                message(STATUS "GLAD fetched, generated for OpenGL core 4.3, and linked.")
                return()
            endif()
        endif()
    endif()

    message(STATUS "GLAD not found. Install glad via vcpkg/package manager or configure with -DPARALLEL_ROAM_FETCH_MISSING_DEPS=ON.")
endfunction()

function(parallel_roam_ensure_stb)
    if(TARGET ParallelROAM::STB OR NOT PARALLEL_ROAM_WITH_STB)
        return()
    endif()

    set(PARALLEL_ROAM_LOCAL_STB_DIR "${PROJECT_SOURCE_DIR}/third_party/stb")
    if(EXISTS "${PARALLEL_ROAM_LOCAL_STB_DIR}/stb_image.h")
        parallel_roam_make_interface_alias(parallel_roam_stb ParallelROAM::STB)
        target_include_directories(parallel_roam_stb INTERFACE "${PARALLEL_ROAM_LOCAL_STB_DIR}")
        message(STATUS "stb local third_party headers found and linked.")
        return()
    endif()

    find_path(PARALLEL_ROAM_STB_INCLUDE_DIR NAMES stb_image.h)

    if(PARALLEL_ROAM_STB_INCLUDE_DIR)
        parallel_roam_make_interface_alias(parallel_roam_stb ParallelROAM::STB)
        target_include_directories(parallel_roam_stb INTERFACE ${PARALLEL_ROAM_STB_INCLUDE_DIR})
        message(STATUS "stb headers found and linked.")
        return()
    endif()

    if(PARALLEL_ROAM_FETCH_MISSING_DEPS)
        FetchContent_Declare(
            stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG ${PARALLEL_ROAM_STB_FETCH_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(stb)

        parallel_roam_make_interface_alias(parallel_roam_stb ParallelROAM::STB)
        target_include_directories(parallel_roam_stb INTERFACE ${stb_SOURCE_DIR})
        message(STATUS "stb fetched with FetchContent and linked.")
        return()
    endif()

    message(STATUS "stb headers not found. Install stb, use vcpkg, or configure with -DPARALLEL_ROAM_FETCH_MISSING_DEPS=ON.")
endfunction()

function(parallel_roam_ensure_imgui)
    if(TARGET ParallelROAM::ImGui OR NOT PARALLEL_ROAM_WITH_IMGUI)
        return()
    endif()

    set(PARALLEL_ROAM_LOCAL_IMGUI_DIR "${PROJECT_SOURCE_DIR}/third_party/imgui")
    if(EXISTS "${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui.cpp")
        parallel_roam_ensure_sdl2()
        if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
            parallel_roam_ensure_opengl()
            set(PARALLEL_ROAM_IMGUI_RENDERER_SOURCE
                ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
        else()
            parallel_roam_ensure_d3d12()
            set(PARALLEL_ROAM_IMGUI_RENDERER_SOURCE
                ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/backends/imgui_impl_dx12.cpp)
        endif()

        add_library(
            parallel_roam_imgui_impl STATIC
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui.cpp
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui_demo.cpp
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui_draw.cpp
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui_tables.cpp
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/imgui_widgets.cpp
            ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
            ${PARALLEL_ROAM_IMGUI_RENDERER_SOURCE}
        )

        target_include_directories(
            parallel_roam_imgui_impl
            PUBLIC
                ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}
                ${PARALLEL_ROAM_LOCAL_IMGUI_DIR}/backends
        )

        if(TARGET ParallelROAM::SDL2)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::SDL2)
        endif()

        if(TARGET ParallelROAM::OpenGL)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::OpenGL)
        endif()

        if(TARGET ParallelROAM::D3D12)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::D3D12)
        endif()

        parallel_roam_make_interface_alias(parallel_roam_imgui ParallelROAM::ImGui)
        target_link_libraries(parallel_roam_imgui INTERFACE parallel_roam_imgui_impl)
        message(STATUS "Dear ImGui local third_party source found and linked.")
        return()
    endif()

    find_package(imgui CONFIG QUIET)

    if(TARGET imgui::imgui)
        parallel_roam_make_interface_alias(parallel_roam_imgui ParallelROAM::ImGui)
        target_link_libraries(parallel_roam_imgui INTERFACE imgui::imgui)
        message(STATUS "Dear ImGui found and linked.")
        return()
    endif()

    if(PARALLEL_ROAM_FETCH_MISSING_DEPS)
        parallel_roam_ensure_sdl2()
        if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
            parallel_roam_ensure_opengl()
        else()
            parallel_roam_ensure_d3d12()
        endif()

        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG ${PARALLEL_ROAM_IMGUI_FETCH_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(imgui)

        if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
            set(PARALLEL_ROAM_IMGUI_RENDERER_SOURCE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
        else()
            set(PARALLEL_ROAM_IMGUI_RENDERER_SOURCE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
        endif()

        add_library(
            parallel_roam_imgui_impl STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
            ${PARALLEL_ROAM_IMGUI_RENDERER_SOURCE}
        )

        target_include_directories(
            parallel_roam_imgui_impl
            PUBLIC
                ${imgui_SOURCE_DIR}
                ${imgui_SOURCE_DIR}/backends
        )

        if(TARGET ParallelROAM::SDL2)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::SDL2)
        endif()

        if(TARGET ParallelROAM::OpenGL)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::OpenGL)
        endif()

        if(TARGET ParallelROAM::D3D12)
            target_link_libraries(parallel_roam_imgui_impl PUBLIC ParallelROAM::D3D12)
        endif()

        parallel_roam_make_interface_alias(parallel_roam_imgui ParallelROAM::ImGui)
        target_link_libraries(parallel_roam_imgui INTERFACE parallel_roam_imgui_impl)
        message(STATUS "Dear ImGui fetched with FetchContent and linked.")
        return()
    endif()

    message(STATUS "Dear ImGui not found. Install imgui, use vcpkg, or configure with -DPARALLEL_ROAM_FETCH_MISSING_DEPS=ON.")
endfunction()

function(parallel_roam_link_if_target target dependency_target compile_definition)
    if(TARGET ${dependency_target})
        target_link_libraries(${target} PRIVATE ${dependency_target})
        target_compile_definitions(${target} PRIVATE ${compile_definition}=1)
    endif()
endfunction()

function(parallel_roam_prepare_dependencies)
    get_property(PARALLEL_ROAM_DEPENDENCIES_PREPARED GLOBAL PROPERTY PARALLEL_ROAM_DEPENDENCIES_PREPARED)
    if(PARALLEL_ROAM_DEPENDENCIES_PREPARED)
        return()
    endif()

    parallel_roam_ensure_sdl2()
    parallel_roam_ensure_glm()
    parallel_roam_ensure_stb()

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
        parallel_roam_ensure_opengl()
        parallel_roam_ensure_glad()
        parallel_roam_ensure_imgui()
    else()
        parallel_roam_ensure_d3d12()
        parallel_roam_ensure_imgui()
    endif()

    set_property(GLOBAL PROPERTY PARALLEL_ROAM_DEPENDENCIES_PREPARED TRUE)
endfunction()

function(parallel_roam_link_optional_dependencies target)
    parallel_roam_prepare_dependencies()

    parallel_roam_link_if_target(${target} ParallelROAM::SDL2 PARALLEL_ROAM_HAS_SDL2)
    parallel_roam_link_if_target(${target} ParallelROAM::GLM PARALLEL_ROAM_HAS_GLM)
    parallel_roam_link_if_target(${target} ParallelROAM::STB PARALLEL_ROAM_HAS_STB)

    if(PARALLEL_ROAM_GRAPHICS_API_NORMALIZED STREQUAL "OPENGL")
        parallel_roam_link_if_target(${target} ParallelROAM::OpenGL PARALLEL_ROAM_HAS_OPENGL)
        parallel_roam_link_if_target(${target} ParallelROAM::GLAD PARALLEL_ROAM_HAS_GLAD)
        parallel_roam_link_if_target(${target} ParallelROAM::ImGui PARALLEL_ROAM_HAS_IMGUI)
    else()
        parallel_roam_link_if_target(${target} ParallelROAM::D3D12 PARALLEL_ROAM_HAS_D3D12)
        parallel_roam_link_if_target(${target} ParallelROAM::ImGui PARALLEL_ROAM_HAS_IMGUI)
    endif()
endfunction()

function(parallel_roam_ensure_d3d12)
    if(TARGET ParallelROAM::D3D12)
        return()
    endif()

    if(NOT WIN32)
        message(STATUS "D3D12 backend is only available on Windows.")
        return()
    endif()

    set(agility_include_dir "${PARALLEL_ROAM_D3D12_AGILITY_SDK_ROOT}/build/native/include")
    if(NOT EXISTS "${agility_include_dir}/d3d12.h")
        message(FATAL_ERROR
            "Pinned D3D12 Agility SDK ${PARALLEL_ROAM_D3D12_AGILITY_SDK_PACKAGE_VERSION} is missing. "
            "Run scripts/setup_dx12_dependencies.ps1 before configuring a D3D12 preset.")
    endif()

    parallel_roam_make_interface_alias(parallel_roam_d3d12 ParallelROAM::D3D12)
    target_include_directories(parallel_roam_d3d12 SYSTEM INTERFACE "${agility_include_dir}")
    target_link_libraries(parallel_roam_d3d12 INTERFACE d3d12 dxgi dxguid version)
    message(STATUS
        "D3D12 system libraries linked with Agility SDK ${PARALLEL_ROAM_D3D12_AGILITY_SDK_PACKAGE_VERSION} headers.")
endfunction()
