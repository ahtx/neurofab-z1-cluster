# Z1 Cluster Controller Firmware
# CMake build configuration for RP2350B with Ethernet

cmake_minimum_required(VERSION 3.13)

# Set platform to RP2350 (Pico 2)
set(PICO_PLATFORM rp2350)
set(PICO_BOARD pico2)

# Include Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(z1_controller_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize Pico SDK
pico_sdk_init()

# Controller firmware executable
add_executable(z1_controller
    z1_http_api.c
    z1_matrix_bus.c
    # Add other controller-specific files here
)

# Link libraries
target_link_libraries(z1_controller
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pio
    hardware_timer
    hardware_clocks
    pico_unique_id
    # Add lwIP or other networking library here
)

# Enable USB output
pico_enable_stdio_usb(z1_controller 1)
pico_enable_stdio_uart(z1_controller 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(z1_controller)

# Include directories
target_include_directories(z1_controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../common
)

# Compiler options
target_compile_options(z1_controller PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -O2
)

# Linker options
target_link_options(z1_controller PRIVATE
    -Wl,--print-memory-usage
)
