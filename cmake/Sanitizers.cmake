function(parallel_roam_enable_sanitizers target)
    if(MSVC)
        message(STATUS "Sanitizer helper is disabled for MSVC in this project scaffold.")
        return()
    endif()

    set(enabled_sanitizers "")

    if(PARALLEL_ROAM_ENABLE_ASAN)
        list(APPEND enabled_sanitizers "address")
    endif()

    if(PARALLEL_ROAM_ENABLE_UBSAN)
        list(APPEND enabled_sanitizers "undefined")
    endif()

    if(enabled_sanitizers)
        list(JOIN enabled_sanitizers "," sanitizer_flags)
        target_compile_options(${target} INTERFACE "-fsanitize=${sanitizer_flags}" -fno-omit-frame-pointer)
        target_link_options(${target} INTERFACE "-fsanitize=${sanitizer_flags}")
    endif()
endfunction()

