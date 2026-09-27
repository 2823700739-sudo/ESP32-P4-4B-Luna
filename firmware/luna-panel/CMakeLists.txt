cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.rev1_3"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(luna_panel)
