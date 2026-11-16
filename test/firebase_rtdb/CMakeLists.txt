cmake_minimum_required(VERSION 3.22)

# Real firebase_rtdb and firebase_auth (auth enabled) over the fake HAL.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(SDKCONFIG_DEFAULTS "${REPO_ROOT}/test/common/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/firebase_rtdb"
    "${REPO_ROOT}/components/firebase_auth"
    "${REPO_ROOT}/components/sse_parser"
    "${REPO_ROOT}/components/utils"
    "${REPO_ROOT}/test/fakes/http_client"
    "${REPO_ROOT}/test/common/unity_main")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_firebase_rtdb)

include(${REPO_ROOT}/cmake/strict_warnings.cmake)
iot_enable_strict_warnings(${REPO_ROOT})
