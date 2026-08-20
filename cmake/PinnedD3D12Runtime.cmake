function(parallel_roam_configure_pinned_d3d12_runtime target out_dxc_executable)
    set(agility_bin_dir "${PARALLEL_ROAM_D3D12_AGILITY_SDK_ROOT}/build/native/bin/x64")
    set(agility_core "${agility_bin_dir}/D3D12Core.dll")
    set(agility_layers "${agility_bin_dir}/d3d12SDKLayers.dll")
    set(dxc_bin_dir "${PARALLEL_ROAM_DXC_ROOT}/build/native/bin/x64")
    set(dxc_executable "${dxc_bin_dir}/dxc.exe")
    set(dxcompiler_library "${dxc_bin_dir}/dxcompiler.dll")
    set(dxil_library "${dxc_bin_dir}/dxil.dll")

    foreach(required_file IN ITEMS
        "${agility_core}"
        "${agility_layers}"
        "${dxc_executable}"
        "${dxcompiler_library}"
        "${dxil_library}")
        if(NOT EXISTS "${required_file}")
            message(FATAL_ERROR
                "Pinned D3D12 dependency is missing: ${required_file}. "
                "Run scripts/setup_dx12_dependencies.ps1 before configuring a D3D12 preset.")
        endif()
    endforeach()

    set(expected_agility_core_sha256 "8A23D826B25B4329522FF451CB52B7F2B34D7F2913CFEB878371CE8BD765FE2D")
    set(expected_agility_layers_sha256 "10A1BB05C914FF8DCEA45B50166759EDB970677E9458C34A1A99F7E0AEE7BE87")
    set(expected_dxc_sha256 "1C9E7CB6C9FB8593E9253FF7FCAE998D2E23A9730722D44229A56497A0D366E7")
    set(expected_dxcompiler_sha256 "570A1A7357893615417EDF5AB356625B5B6B721A131BC7331BF17289D4928ED7")
    set(expected_dxil_sha256 "9CCCC7EF419DA73FA314FDAECAE831C6C20206AE70732C9093F95193378CED10")

    foreach(name IN ITEMS agility_core agility_layers dxc dxcompiler dxil)
        if(name STREQUAL "agility_core")
            set(file_path "${agility_core}")
        elseif(name STREQUAL "agility_layers")
            set(file_path "${agility_layers}")
        elseif(name STREQUAL "dxc")
            set(file_path "${dxc_executable}")
        elseif(name STREQUAL "dxcompiler")
            set(file_path "${dxcompiler_library}")
        else()
            set(file_path "${dxil_library}")
        endif()

        set(expected_sha256_variable "expected_${name}_sha256")
        set(expected_sha256 "${${expected_sha256_variable}}")
        file(SHA256 "${file_path}" actual_sha256)
        string(TOUPPER "${actual_sha256}" actual_sha256)
        if(NOT "${actual_sha256}" STREQUAL "${expected_sha256}")
            message(FATAL_ERROR
                "Pinned D3D12 dependency hash mismatch: ${file_path}. "
                "Run scripts/setup_dx12_dependencies.ps1 -Force to restore the official package.")
        endif()
    endforeach()

    execute_process(
        COMMAND "${dxc_executable}" --version
        RESULT_VARIABLE dxc_result
        OUTPUT_VARIABLE dxc_version
        ERROR_VARIABLE dxc_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT dxc_result EQUAL 0 OR
       NOT dxc_version MATCHES "1\\.7\\.2308\\.7" OR
       NOT dxc_version MATCHES "101\\.7\\.2308\\.12")
        message(FATAL_ERROR
            "Pinned DXC version validation failed. Output: ${dxc_version} ${dxc_error}")
    endif()

    target_compile_definitions(
        ${target}
        PRIVATE PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION=${PARALLEL_ROAM_D3D12_AGILITY_SDK_VERSION})

    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${agility_core}"
            "$<TARGET_FILE_DIR:${target}>/D3D12/D3D12Core.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${agility_layers}"
            "$<TARGET_FILE_DIR:${target}>/D3D12/d3d12SDKLayers.dll"
        COMMENT
            "Copying pinned D3D12 Agility SDK ${PARALLEL_ROAM_D3D12_AGILITY_SDK_PACKAGE_VERSION} runtime"
    )

    message(STATUS
        "Pinned D3D12 runtime: Agility SDK ${PARALLEL_ROAM_D3D12_AGILITY_SDK_PACKAGE_VERSION}, "
        "DXC ${PARALLEL_ROAM_DXC_PACKAGE_VERSION}")
    set(${out_dxc_executable} "${dxc_executable}" PARENT_SCOPE)
endfunction()
