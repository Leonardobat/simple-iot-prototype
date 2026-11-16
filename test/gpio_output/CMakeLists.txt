cmake_minimum_required(VERSION 3.22)

# Real gpio_output HAL in QEMU (no fake).
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/test/common/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/gpio_output"
    "${REPO_ROOT}/test/common/unity_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_gpio_output)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
