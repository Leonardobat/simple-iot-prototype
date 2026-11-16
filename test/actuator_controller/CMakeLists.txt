cmake_minimum_required(VERSION 3.22)

# Real actuator_controller over the fake output HALs (rgb_led, gpio_output).
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/test/common/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/actuator_controller"
    "${REPO_ROOT}/components/json_writer"
    "${REPO_ROOT}/components/utils"
    "${REPO_ROOT}/test/fakes/rgb_led"
    "${REPO_ROOT}/test/fakes/gpio_output"
    "${REPO_ROOT}/test/common/unity_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_actuator_controller)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
