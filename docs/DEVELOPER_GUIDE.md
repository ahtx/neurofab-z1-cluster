# NeuroFab Z1 - Developer Guide

**Version:** 1.0  
**Date:** November 12, 2025

---

## 1. Introduction

This guide provides comprehensive instructions for developers working with the NeuroFab Z1 cluster firmware. It covers everything from setting up the development environment to building, flashing, and debugging the firmware.

---

## 2. Development Environment Setup

### 2.1. Prerequisites

- **ARM GCC Toolchain:** `arm-none-eabi-gcc`
- **CMake:** Version 3.13 or higher
- **Pico SDK:** Raspberry Pi Pico SDK
- **Python 3:** For running build scripts and tools

### 2.2. Installation

```bash
# Install build tools
sudo apt-get update
sudo apt-get install -y cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
export PICO_SDK_PATH=$(pwd)/pico-sdk
```

---

## 3. Building Firmware

The firmware is built using CMake. The root `CMakeLists.txt` in `embedded_firmware/` orchestrates the build process for all components.

### 3.1. Build Steps

```bash
# Navigate to firmware directory
cd embedded_firmware

# Create build directory
mkdir build && cd build

# Configure CMake
cmake ..

# Build all targets
make -j$(nproc)
```

### 3.2. Build Targets

- `z1_node`: Node firmware (`z1_node.uf2`)
- `z1_controller`: Controller firmware (`z1_controller.uf2`)
- `z1_bootloader`: Bootloader (`z1_bootloader.uf2`)

---

## 4. Flashing Procedures

### 4.1. USB Flashing (Initial Setup)

1. Connect the RP2350B to your computer via USB while holding the BOOTSEL button.
2. The board will mount as a USB mass storage device.
3. Drag and drop the `.uf2` file onto the device.

### 4.2. Network Flashing (Firmware Updates)

The `nflash` utility allows you to update firmware over the network.

```bash
# Flash node firmware to all nodes
nflash flash z1_node.uf2 all

# Flash controller firmware
nflash flash z1_controller.uf2 controller
```

---

## 5. Firmware Architecture

### 5.1. Bootloader

- **Location:** `bootloader/`
- **Size:** 16KB (protected)
- **Function:** Handles firmware updates, CRC verification, and boot mode selection.

### 5.2. Application Firmware

- **Location:** `node/` and `controller/`
- **Size:** 112KB
- **Function:** Main application logic (SNN engine, HTTP server, etc.)

### 5.3. Memory Layout

| Address Range       | Size   | Description                |
|---------------------|--------|----------------------------|
| `0x10000000`        | 16KB   | Bootloader (protected)     |
| `0x10004000`        | 112KB  | Application Firmware       |
| `0x10020000`        | 128KB  | Firmware Update Buffer     |
| `0x20000000`        | 8MB    | PSRAM (neuron tables, etc.)|

---

## 6. Integration with Existing Code

The firmware is designed to be modular. To integrate with your existing codebase:

1. **Copy source files:** Copy the `node/` and `controller/` directories into your project.
2. **Update CMakeLists:** Add the new directories to your `CMakeLists.txt`.
3. **Resolve dependencies:** Ensure the Pico SDK and other libraries are correctly linked.
4. **Merge main loops:** Integrate the SNN engine and HTTP server into your main application loop.

---

## 7. STDP Implementation Details

- **File:** `emulator/core/snn_engine_stdp.py` (Python reference)
- **Algorithm:** Exponential STDP with configurable learning rates and time constants.
- **Hardware Porting:** The Python implementation can be used as a reference for porting STDP to C for the hardware firmware.

---

## 8. Testing and Debugging

- **Debug Output:** Enable debug flags in `CMakeLists.txt` to get detailed logging over UART.
- **Unit Tests:** Use a framework like Ceedling to write unit tests for individual modules.
- **Integration Tests:** Use the Python tools to test the complete system.

---

## 9. Performance Optimization

- **Fixed-Point Math:** Use fixed-point arithmetic for neuron calculations to avoid floating-point overhead.
- **DMA:** Use DMA for PSRAM access to free up the CPU.
- **Lookup Tables:** Use lookup tables for exponential calculations in STDP.
