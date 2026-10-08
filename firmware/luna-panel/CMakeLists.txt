cmake_minimum_required(VERSION 3.16)
option(LUNA_BLE_B0_BUILD "Secured Luna BLE transport" ON)
option(LUNA_BLE_B1_BUILD "Current five-card Luna product" ON)
option(LUNA_PUBLIC_RELEASE "Build without personal Wi-Fi configuration" OFF)
if(NOT DEFINED SDKCONFIG)
    if(LUNA_PUBLIC_RELEASE)
        set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.public_b1" CACHE FILEPATH "Credential-free public configuration")
    elseif(LUNA_BLE_B1_BUILD)
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

if(LUNA_PUBLIC_RELEASE)
    if(NOT LUNA_BLE_B1_BUILD)
        message(FATAL_ERROR "Public release requires the B1 product")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.public")
    set(PROJECT_VER "2026.10.08-preview")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(luna_panel)

if(LUNA_PUBLIC_RELEASE)
    if(NOT "${CONFIG_LUNA_WIFI_SSID}" STREQUAL "" OR NOT "${CONFIG_LUNA_WIFI_PASSWORD}" STREQUAL "")
        message(FATAL_ERROR "Public release must not contain Wi-Fi credentials")
    endif()
endif()
