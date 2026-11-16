cmake_minimum_required(VERSION 3.22)

# Real reading_store over the fake flash_fs HAL. sensor_provider and actuator_controller are real
# (only their types and counts are used) over their own fake HALs.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/test/common/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/reading_store"
    "${REPO_ROOT}/components/sensor_provider"
    "${REPO_ROOT}/components/actuator_controller"
    "${REPO_ROOT}/components/json_writer"
    "${REPO_ROOT}/components/utils"
    "${REPO_ROOT}/test/fakes/flash_fs"
    "${REPO_ROOT}/test/fakes/chip_temperature"
    "${REPO_ROOT}/test/fakes/analog_input"
    "${REPO_ROOT}/test/fakes/rgb_led"
    "${REPO_ROOT}/test/fakes/gpio_output"
    "${REPO_ROOT}/test/common/unity_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_reading_store)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
