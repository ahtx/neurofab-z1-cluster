# CMakeLists.txt for Z1 Bootloader

cmake_minimum_required(VERSION 3.13)

# Set project name
project(z1_bootloader C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
# (Assumes PICO_SDK_PATH is set in environment)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

pico_sdk_init()

# Bootloader executable
add_executable(z1_bootloader
    z1_bootloader.c
)

# Link libraries
target_link_libraries(z1_bootloader
    pico_stdlib
    hardware_flash
    hardware_sync
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(z1_bootloader)

# Set linker script (custom for bootloader)
pico_set_linker_script(z1_bootloader ${CMAKE_CURRENT_LIST_DIR}/bootloader.ld)

# Compile options
target_compile_options(z1_bootloader PRIVATE
    -Wall
    -Wextra
    -Os  # Optimize for size
)
