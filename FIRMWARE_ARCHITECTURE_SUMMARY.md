# Firmware Architecture Summary

## Question: Does this system have generic loaded firmware?

**Answer: YES** - The system now has a **two-stage bootloader architecture** that supports loading custom firmware remotely.

## Architecture

### Current Design (Enhanced)

```
┌─────────────────────────────────────────────────────────┐
│                  RP2350B Flash (2MB)                    │
├─────────────────────────────────────────────────────────┤
│ 0x10000000  BOOTLOADER (16KB) [PROTECTED]              │
│             - Always present, cannot be overwritten     │
│             - Handles firmware updates                  │
│             - Provides API to applications              │
│             - Boot mode selection                       │
├─────────────────────────────────────────────────────────┤
│ 0x10004000  APPLICATION FIRMWARE (112KB) [UPDATABLE]   │
│             - Your custom code                          │
│             - Can be changed via nflash utility         │
│             - Different firmware per node possible      │
│             - Examples:                                 │
│               * snn_firmware.bin (neural networks)      │
│               * matrix_firmware.bin (matrix ops)        │
│               * signal_firmware.bin (signal processing) │
│               * custom_firmware.bin (your algorithm)    │
├─────────────────────────────────────────────────────────┤
│ 0x10020000  FIRMWARE UPDATE BUFFER (128KB)             │
│             - Temporary storage during update           │
└─────────────────────────────────────────────────────────┘
```

## Can nflash Flash New Firmware?

**YES** - The `nflash` utility allows you to flash new firmware.bin files to change the programming on each processor.

### Usage Examples

```bash
# Flash firmware to single node
nflash flash my_algorithm.bin 0 --name "My Algorithm" --version "1.0.0"

# Flash to all nodes
nflash flash snn_firmware.bin all

# Flash to specific backplane
nflash flash matrix_firmware.bin backplane-0:all

# Flash different firmware to different nodes
nflash flash snn_firmware.bin 0-7        # Nodes 0-7: SNN
nflash flash matrix_firmware.bin 8-15   # Nodes 8-15: Matrix ops

# Check what firmware is running
nflash info all
```

## How It Works

### 1. Firmware Upload

```
User PC                Controller              Node Bootloader
   |                       |                         |
   |--nflash flash-------->|                         |
   |  firmware.bin         |                         |
   |                       |                         |
   |                       |--Z1_CMD_FIRMWARE_UPLOAD>|
   |                       |  (firmware data)        |
   |                       |                         |
   |                       |<-------ACK--------------|
   |<------Success---------|                         |
```

### 2. Firmware Installation

The bootloader on each node:
1. Receives firmware in update buffer (0x10020000)
2. Verifies CRC32 checksum
3. Erases application slot (0x10004000)
4. Copies verified firmware to application slot
5. Marks firmware as valid
6. Reboots

### 3. Boot Process

On every boot, the bootloader:
1. Checks if valid application firmware exists
2. Verifies CRC32
3. If valid: Jumps to application entry point
4. If invalid: Stays in bootloader mode (safe!)

## Benefits

### 1. Remote Updates
- No physical access needed
- Update hundreds of nodes in minutes
- Deploy from anywhere on the network

### 2. Flexibility
- Run different algorithms on different nodes
- Example: Nodes 0-7 do SNN, nodes 8-15 do matrix ops
- Change algorithms without hardware modifications

### 3. Safety
- Bootloader is protected (cannot be overwritten)
- CRC verification prevents corrupted firmware
- Always recoverable if firmware fails

### 4. Development Speed
- Iterate quickly on algorithms
- Deploy and test in seconds
- No JTAG programmer needed

## Firmware Development Workflow

### 1. Write Your Firmware

```c
#include "../bootloader/z1_bootloader.h"

void app_entry(void) {
    z1_bootloader_api_t *api = GET_BOOTLOADER_API();
    
    // Your algorithm here
    while (1) {
        // Process data
        // Use api->bus_send(), api->psram_read(), etc.
    }
}
```

### 2. Build

```bash
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 \
    -T app_firmware.ld \
    -o my_firmware.elf \
    my_firmware.c

arm-none-eabi-objcopy -O binary my_firmware.elf my_firmware.bin
```

### 3. Deploy

```bash
nflash flash my_firmware.bin all --name "My Algorithm" --version "1.0.0"
```

### 4. Verify

```bash
nflash info all
```

## Example Use Cases

### Use Case 1: Heterogeneous Cluster

```bash
# Deploy SNN to first 8 nodes
nflash flash snn_firmware.bin backplane-0:0-7

# Deploy matrix multiplication to next 8 nodes
nflash flash matrix_firmware.bin backplane-0:8-15

# Now you have a mixed-workload cluster!
```

### Use Case 2: A/B Testing

```bash
# Deploy algorithm A to half the nodes
nflash flash algorithm_a.bin backplane-0:0-7

# Deploy algorithm B to other half
nflash flash algorithm_b.bin backplane-0:8-15

# Compare performance
```

### Use Case 3: Rapid Prototyping

```bash
# Try version 1
nflash flash prototype_v1.bin 0
# Test...

# Try version 2
nflash flash prototype_v2.bin 0
# Test...

# Deploy winning version to all
nflash flash prototype_v2.bin all
```

## Comparison: Before vs. After

| Feature | Before (Fixed Firmware) | After (Bootloader Architecture) |
|---------|------------------------|----------------------------------|
| Firmware Update | JTAG programmer required | Network (nflash utility) |
| Physical Access | Required | Not required |
| Update Time | Minutes per node | Seconds for all nodes |
| Different Algorithms | Not possible | Yes, per-node flexibility |
| Recovery | Hardware programmer | Automatic (bootloader) |
| Experimentation | Slow, risky | Fast, safe |

## Documentation

- **[Firmware Development Guide](docs/firmware_development_guide.md)** - Complete guide to creating custom firmware
- **[System Integration Guide](docs/system_integration_guide.md)** - How bootloader integrates with system
- **[Bootloader Header](embedded_firmware/bootloader/z1_bootloader.h)** - API reference
- **[Application Template](embedded_firmware/app_template/app_main.c)** - Starting point for custom firmware

## Conclusion

**Yes**, the system now has a generic bootloader that allows you to flash new firmware.bin files to change the programming on each processor node remotely using the `nflash` utility. This provides maximum flexibility while maintaining safety through the protected bootloader architecture.
