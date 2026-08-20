function(parallel_roam_set_project_warnings target)
    if(NOT PARALLEL_ROAM_ENABLE_WARNINGS)
        return()
    endif()

    set(msvc_warnings
        /W4
        /permissive-
        /Zc:preprocessor
    )

    set(clang_gcc_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
    )

    if(MSVC)
        target_compile_options(${target} INTERFACE ${msvc_warnings})

        if(PARALLEL_ROAM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE /WX)
        endif()
    else()
        target_compile_options(${target} INTERFACE ${clang_gcc_warnings})

        if(PARALLEL_ROAM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE -Werror)
        endif()
    endif()
endfunction()

