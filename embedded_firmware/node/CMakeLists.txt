# Z1 Neuromorphic Compute Node Firmware
# CMake build configuration for RP2350B

cmake_minimum_required(VERSION 3.13)

# Set platform to RP2350 (Pico 2)
set(PICO_PLATFORM rp2350)
set(PICO_BOARD pico2)

# Include Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(z1_node_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize Pico SDK
pico_sdk_init()

# Node firmware executable
add_executable(z1_node
    node.c
    z1_snn_engine.c
    psram_rp2350.c
    z1_matrix_bus.c
    ssd1306.c
    # hw_config.c  # SD card support - requires FatFS library
)

# Link libraries
target_link_libraries(z1_node
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_timer
    hardware_clocks
    hardware_gpio
    pico_unique_id
)

# Enable USB output
pico_enable_stdio_usb(z1_node 1)
pico_enable_stdio_uart(z1_node 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(z1_node)

# Include directories
target_include_directories(z1_node PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../common
)

# Compiler options
target_compile_options(z1_node PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -O2
)

# Linker options
target_link_options(z1_node PRIVATE
    -Wl,--print-memory-usage
)
