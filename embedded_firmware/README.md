# NeuroFab Z1 Embedded Firmware

This directory contains the embedded firmware for the NeuroFab Z1 cluster nodes.

## Components

- **node/** - Node firmware (SNN execution engine)
- **controller/** - Controller firmware (HTTP API server)
- **bootloader/** - Two-stage bootloader
- **common/** - Shared protocol definitions

## Building

```bash
# Prerequisites
sudo apt-get install cmake gcc-arm-none-eabi

# Set Pico SDK path
export PICO_SDK_PATH=/path/to/pico-sdk

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Outputs

- `z1_node.uf2` - Node firmware
- `z1_controller.uf2` - Controller firmware
- `z1_bootloader.uf2` - Bootloader

## Flashing

### USB Flashing (Initial)

1. Hold BOOTSEL button while connecting USB
2. Drag and drop `.uf2` file to mounted device

### Network Flashing (Updates)

```bash
nflash flash z1_node.uf2 all
```

For detailed instructions, see [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) and the [Developer Guide](../docs/DEVELOPER_GUIDE.md).
