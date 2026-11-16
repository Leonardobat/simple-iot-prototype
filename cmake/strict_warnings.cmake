# Strict warnings for the project's own code (components/, main/, test/), never for ESP-IDF or
# managed components. Call iot_enable_strict_warnings(<repo root>) right after project().
#
# -Wpedantic is deliberately absent: ESP-IDF 6.1's own headers fail it (#include_next in
# esp_libc/platform_include, empty structs in sys/reent.h, __FUNCTION__ in esp_check.h).
# ESP-IDF downgrades several warnings (-Wno-error=extra, -Wno-sign-compare, -Wno-unused-parameter,
# ...); they are restored here as errors.
set(IOT_STRICT_WARNINGS
    -Wall
    -Wextra
    -Werror
    -Werror=extra
    -Wsign-compare
    -Wunused-parameter
    -Wshadow
    -Wformat=2
    -Werror=unused-function
    -Werror=unused-variable
    -Werror=unused-but-set-variable
    -Werror=deprecated-declarations)

function(iot_enable_strict_warnings repo_root)
    cmake_path(NORMAL_PATH repo_root)
    idf_build_get_property(components BUILD_COMPONENTS)
    foreach(component ${components})
        idf_component_get_property(dir ${component} COMPONENT_DIR)
        idf_component_get_property(lib ${component} COMPONENT_LIB)
        cmake_path(IS_PREFIX repo_root "${dir}" NORMALIZE is_project_code)
        if(NOT is_project_code OR dir MATCHES "/managed_components/")
            continue()
        endif()
        get_target_property(lib_type ${lib} TYPE)
        if(NOT lib_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${lib} PRIVATE ${IOT_STRICT_WARNINGS})
        endif()
    endforeach()
endfunction()
