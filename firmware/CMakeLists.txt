cmake_minimum_required(VERSION 3.16)

# Set project name
set(PROJECT_NAME "ECU_P4_Pro-Spec")

# Set ESP-IDF path
set(ESP_IDF_PATH "C:/Users/Pedro/.espressif")

# Include ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

# Set project
project(${PROJECT_NAME})

# Add components
idf_component_register(
    SRC_DIRS main components/p4_control components/c6_supervision
    INCLUDE_DIRS main components/p4_control components/c6_supervision
    REQUIRES mcpwm rmt spi can fatfs freertos
    EMBED_FILES
)

# Set build configuration
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g -Wall -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -Wall -Werror")

# Set memory regions
set(ESP32_P4_MEMORY_REGIONS "iram0_0_seg:dram0_0_seg:dram0_1_seg")
set(ESP32_C6_MEMORY_REGIONS "iram0_0_seg:dram0_0_seg:dram0_1_seg")

# Set optimization flags
set(CMAKE_C_FLAGS_RELEASE "-O2 -g -fno-strict-aliasing -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -fno-strict-aliasing -ffunction-sections -fdata-sections")

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# Set build directory
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build)