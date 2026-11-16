cmake_minimum_required(VERSION 3.22)

# Real sensor_provider over the fake chip_temperature and analog_input HALs (LDR on GPIO4 by
# default), and esp_random() for the mocks.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/test/common/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/sensor_provider"
    "${REPO_ROOT}/components/json_writer"
    "${REPO_ROOT}/components/utils"
    "${REPO_ROOT}/test/fakes/chip_temperature"
    "${REPO_ROOT}/test/fakes/analog_input"
    "${REPO_ROOT}/test/common/unity_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_sensor_provider)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
