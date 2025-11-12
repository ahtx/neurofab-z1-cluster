# Z1 Neuromorphic Cluster Firmware
# Root CMake configuration

cmake_minimum_required(VERSION 3.13)

# Set Pico SDK path (override with environment variable if needed)
if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH environment variable not set. Please set it to your Pico SDK installation directory.")
endif()

# Include Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(z1_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize Pico SDK
pico_sdk_init()

# Add subdirectories
add_subdirectory(node)
add_subdirectory(controller)
add_subdirectory(bootloader)
