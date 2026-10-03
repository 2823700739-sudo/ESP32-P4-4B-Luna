cmake_minimum_required(VERSION 3.16)
option(LUNA_BLE_B0_BUILD "Secured Luna BLE transport" ON)
option(LUNA_BLE_B1_BUILD "Current five-card Luna product" ON)
if(NOT DEFINED SDKCONFIG)
    if(LUNA_BLE_B1_BUILD)
        set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.ble_b1" CACHE FILEPATH "Luna product configuration")
    else()
        set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.ble_b0" CACHE FILEPATH "Luna BLE diagnostic configuration")
    endif()
endif()

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.rev1_3"
)

# Isolated B0 image: no old USB/HTTP application, separate sdkconfig/build dir.
if(LUNA_BLE_B0_BUILD)
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.ble_b0")
    if(LUNA_BLE_B1_BUILD)
        list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.ble_b1")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(luna_panel)
