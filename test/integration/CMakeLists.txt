cmake_minimum_required(VERSION 3.22)

# Board-only integration tests: real Wi-Fi, Firebase Auth and Realtime Database.
# Not run by run_unit_tests.sh (QEMU has no Wi-Fi). Run by pytest-embedded (test_integration.py),
# which picks each case from the Unity menu (docs/adr/0006-pytest-embedded-integracao.md).
#
# Config is layered so credentials are never duplicated: product defaults, then the
# product's local sdkconfig (Wi-Fi/Firebase credentials), then the overrides here.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/sdkconfig.defaults")
if(EXISTS "${REPO_ROOT}/sdkconfig")
    list(APPEND SDKCONFIG_DEFAULTS "${REPO_ROOT}/sdkconfig")
endif()
list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

# The layers above only seed a missing sdkconfig, so this project's sdkconfig is treated as
# derived: whenever a layer is newer (e.g. credentials changed in the product menuconfig),
# it is deleted and regenerated. CMAKE_CONFIGURE_DEPENDS makes `idf.py build` notice the change.
set(DERIVED_SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
foreach(layer ${SDKCONFIG_DEFAULTS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${layer}")
    if(EXISTS "${DERIVED_SDKCONFIG}" AND "${layer}" IS_NEWER_THAN "${DERIVED_SDKCONFIG}")
        message(STATUS "${layer} changed: regenerating ${DERIVED_SDKCONFIG}")
        file(REMOVE "${DERIVED_SDKCONFIG}")
    endif()
endforeach()

set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components"
    "${REPO_ROOT}/test/common/unity_menu_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_integration)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
