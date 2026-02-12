cmake_minimum_required(VERSION 3.16)

# Exclude connectivity on C6 (depends on esp_bt, not available for esp32c6)
set(EXCLUDE_COMPONENTS connectivity)

# Include ESP-IDF cmake project setup BEFORE project()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ecu_c6_firmware)
