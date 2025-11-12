# Firmware Development Guide for Z1 Compute Nodes

**Author:** NeuroFab  
**Date:** November 11, 2025

## Introduction

The NeuroFab Z1 compute nodes use a **two-stage firmware architecture** that separates the bootloader from the application firmware. This design allows you to update the application firmware remotely without needing physical access to the hardware, enabling rapid development and deployment of custom algorithms.

## Architecture Overview

### Two-Stage Design

```
┌─────────────────────────────────────────────────────────┐
│                    RP2350B Flash (2MB)                  │
├─────────────────────────────────────────────────────────┤
│ 0x10000000  Bootloader (16KB)         [PROTECTED]      │
│             - Z1 bus handler                            │
│             - Firmware update logic                     │
│             - Boot selection                            │
│             - API for applications                      │
├─────────────────────────────────────────────────────────┤
│ 0x10004000  Application Firmware (112KB) [UPDATABLE]   │
│             - Your custom code                          │
│             - SNN engine, matrix ops, etc.              │
│             - Uses bootloader API                       │
├─────────────────────────────────────────────────────────┤
│ 0x10020000  Firmware Update Buffer (128KB)             │
│             - Temporary storage for new firmware        │
├─────────────────────────────────────────────────────────┤
│ 0x10040000  User Data / Filesystem (1.75MB)            │
└─────────────────────────────────────────────────────────┘
```

### Why This Design?

**Benefits:**
- **Remote Updates**: Flash new firmware over the network
- **Safety**: Bootloader is protected, always recoverable
- **Flexibility**: Run different workloads on different nodes
- **Development Speed**: Iterate quickly without hardware access

**Trade-offs:**
- Application firmware limited to 112KB (plenty for most use cases)
- Small overhead from bootloader API calls
- Must follow specific memory layout

## Firmware Structure

Every application firmware consists of three parts:

### 1. Firmware Header (256 bytes)

The bootloader expects a specific header at the start of the firmware:

```c
typedef struct {
    uint32_t magic;              // 0x4E465A31 ("NFZ1")
    uint32_t version;            // Header version (1)
    uint32_t firmware_size;      // Size of code (bytes)
    uint32_t entry_point;        // Entry offset from 0x10004000
    uint32_t crc32;              // CRC32 checksum
    uint32_t timestamp;          // Build time (Unix timestamp)
    char     name[32];           // Firmware name
    char     version_string[16]; // Version (e.g., "1.0.0")
    uint32_t capabilities;       // Capability flags
    uint32_t reserved[42];       // Reserved
} z1_firmware_header_t;
```

**You don't create this manually** - the `nflash` utility generates it automatically.

### 2. Vector Table

The ARM Cortex-M33 requires a vector table for interrupts and exceptions:

```c
__attribute__((section(".app_vectors")))
const void *app_vectors[] = {
    (void*)0x20080000,  // Initial stack pointer
    app_entry,          // Reset handler (your entry point)
    // Add more handlers as needed
};
```

### 3. Application Code

Your actual firmware logic:

```c
void app_entry(void) {
    app_init();
    app_main();  // Main loop
}
```

## Using the Bootloader API

The bootloader provides services to your application firmware through a function pointer table.

### Accessing the API

```c
#include "../bootloader/z1_bootloader.h"

z1_bootloader_api_t *api = GET_BOOTLOADER_API();
```

### Available Functions

**Z1 Bus Communication:**
```c
// Send data to another node
api->bus_send(node_id, data, length);

// Receive data from bus
uint16_t actual_len;
if (api->bus_receive(buffer, max_len, &actual_len)) {
    // Process received data
}
```

**PSRAM Access:**
```c
// Write to PSRAM
api->psram_write(0x20100000, data, length);

// Read from PSRAM
api->psram_read(0x20100000, buffer, length);
```

**Flash Access:**
```c
// Write configuration to flash
api->flash_write(USER_DATA_BASE, config, sizeof(config));

// Read configuration
api->flash_read(USER_DATA_BASE, config, sizeof(config));
```

**System Functions:**
```c
// Get current time
uint32_t time_ms = api->get_system_time_ms();

// Reboot the node
api->system_reset();

// Calculate CRC32
uint32_t crc = api->crc32(data, length);
```

## Creating Custom Firmware

### Step 1: Start with the Template

```bash
cd embedded_firmware/app_template
cp app_main.c my_firmware.c
```

### Step 2: Implement Your Logic

```c
void app_init(void) {
    g_bootloader_api = GET_BOOTLOADER_API();
    
    // Initialize your application
    // - Set up data structures
    // - Configure PSRAM
    // - Initialize state
}

void app_main(void) {
    while (1) {
        // Your main loop
        
        // Example: Process messages from Z1 bus
        uint8_t buffer[256];
        uint16_t len;
        
        if (g_bootloader_api->bus_receive(buffer, 256, &len)) {
            process_message(buffer, len);
        }
        
        // Example: Update state in PSRAM
        update_psram_data();
        
        // Small delay
        delay_ms(10);
    }
}
```

### Step 3: Build the Firmware

```bash
# Compile
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 \
    -T app_firmware.ld \
    -o my_firmware.elf \
    my_firmware.c

# Extract binary
arm-none-eabi-objcopy -O binary my_firmware.elf my_firmware.bin
```

### Step 4: Flash to Nodes

```bash
# Flash to single node
nflash flash my_firmware.bin 0 --name "My Firmware" --version "1.0.0"

# Flash to all nodes
nflash flash my_firmware.bin all

# Flash to specific backplane
nflash flash my_firmware.bin backplane-0:all
```

## Example: Matrix Multiplication Firmware

Let's create firmware that performs distributed matrix multiplication.

### matrix_firmware.c

```c
#include "../bootloader/z1_bootloader.h"
#include <stdint.h>
#include <string.h>

#define MATRIX_SIZE 64  // 64x64 matrices

// Store matrices in PSRAM
__attribute__((section(".psram_data")))
float matrix_a[MATRIX_SIZE][MATRIX_SIZE];

__attribute__((section(".psram_data")))
float matrix_b[MATRIX_SIZE][MATRIX_SIZE];

__attribute__((section(".psram_data")))
float matrix_result[MATRIX_SIZE][MATRIX_SIZE];

static z1_bootloader_api_t *api;
static uint8_t node_id;
static uint8_t total_nodes;

void app_init(void) {
    api = GET_BOOTLOADER_API();
    
    // Node ID is received from controller during initialization
    // For now, hardcode or read from flash
    node_id = 0;
    total_nodes = 16;
    
    // Clear result matrix
    memset(matrix_result, 0, sizeof(matrix_result));
}

void compute_partial_matrix(uint8_t row_start, uint8_t row_end) {
    // Each node computes a subset of rows
    for (int i = row_start; i < row_end; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            float sum = 0.0f;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                sum += matrix_a[i][k] * matrix_b[k][j];
            }
            matrix_result[i][j] = sum;
        }
    }
}

void handle_command(uint8_t *data, uint16_t len) {
    uint8_t cmd = data[0];
    
    switch (cmd) {
        case 0x01:  // Load matrix A
            memcpy(matrix_a, data + 1, sizeof(matrix_a));
            api->bus_send(node_id, "ACK", 3);
            break;
        
        case 0x02:  // Load matrix B
            memcpy(matrix_b, data + 1, sizeof(matrix_b));
            api->bus_send(node_id, "ACK", 3);
            break;
        
        case 0x03:  // Compute
            {
                // Divide work among nodes
                uint8_t rows_per_node = MATRIX_SIZE / total_nodes;
                uint8_t row_start = node_id * rows_per_node;
                uint8_t row_end = row_start + rows_per_node;
                
                compute_partial_matrix(row_start, row_end);
                
                // Send result back
                uint8_t response[1 + sizeof(matrix_result)];
                response[0] = 0x80;  // Result command
                memcpy(response + 1, matrix_result, sizeof(matrix_result));
                api->bus_send(node_id, response, sizeof(response));
            }
            break;
    }
}

void app_main(void) {
    uint8_t buffer[65536];
    uint16_t len;
    
    while (1) {
        if (api->bus_receive(buffer, sizeof(buffer), &len)) {
            handle_command(buffer, len);
        }
    }
}

void app_entry(void) {
    app_init();
    app_main();
}

__attribute__((section(".app_vectors")))
const void *app_vectors[] = {
    (void*)0x20080000,
    app_entry,
};
```

### Build and Deploy

```bash
# Build
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 \
    -T app_firmware.ld \
    -o matrix_firmware.elf \
    matrix_firmware.c

arm-none-eabi-objcopy -O binary matrix_firmware.elf matrix_firmware.bin

# Flash to all nodes
nflash flash matrix_firmware.bin all \
    --name "Matrix Multiply" \
    --version "1.0.0"
```

## Firmware Update Workflow

### From User Perspective

```bash
# 1. Check current firmware
nflash info all

# 2. Flash new firmware
nflash flash new_firmware.bin all

# 3. Nodes automatically reboot with new firmware

# 4. Verify
nflash info all
```

### Behind the Scenes

1. **Upload**: `nflash` sends firmware to controller via HTTP
2. **Controller**: Forwards firmware to node via Z1 bus command `Z1_CMD_FIRMWARE_UPLOAD`
3. **Node Bootloader**: Writes firmware to update buffer (0x10020000)
4. **Verify**: Bootloader checks CRC32
5. **Install**: Bootloader copies from buffer to application slot (0x10004000)
6. **Reboot**: Node reboots
7. **Boot**: Bootloader verifies application and jumps to entry point

### Safety Features

- **CRC Verification**: Corrupted firmware is rejected
- **Atomic Update**: Old firmware remains until new firmware is fully verified
- **Recovery**: If new firmware fails to boot, bootloader stays active
- **Protected Bootloader**: Bootloader itself cannot be overwritten remotely

## Memory Usage Guidelines

### Flash (Application Slot: 112KB)

**Typical Usage:**
- Code: 40-80KB
- Const data: 10-20KB
- Remaining: 12-62KB

**Tips:**
- Use `-O2` or `-Os` optimization
- Store large lookup tables in PSRAM, not flash
- Remove unused functions with `-ffunction-sections -Wl,--gc-sections`

### SRAM (512KB)

**Typical Usage:**
- Stack: 16-32KB
- Heap: 64-128KB
- Global variables: 10-50KB
- Remaining: 302-422KB

**Tips:**
- Use PSRAM for large buffers
- Keep stack usage minimal (avoid large local arrays)

### PSRAM (7MB available to application)

**Typical Usage:**
- Neuron tables: 1-4MB
- Matrix data: 1-2MB
- Buffers: 0.5-1MB
- Remaining: 0-4.5MB

**Tips:**
- Use `__attribute__((section(".psram_data")))` for large arrays
- PSRAM is slower than SRAM, but much larger
- Good for: neuron states, weight matrices, large buffers

## Debugging

### Serial Output

```c
// Use bootloader API to send debug messages over Z1 bus
char debug_msg[64];
snprintf(debug_msg, sizeof(debug_msg), "Debug: value=%d\n", value);
api->bus_send(node_id, debug_msg, strlen(debug_msg));
```

### Reading Memory Remotely

```bash
# Read application memory
ncat 0@0x10004000 -l 256  # Read first 256 bytes of firmware

# Read PSRAM
ncat 0@0x20100000 -l 1024  # Read neuron table
```

### Firmware Info

```bash
# Get detailed firmware information
nflash info all
```

## Best Practices

### 1. Always Test on Single Node First

```bash
# Test on node 0
nflash flash test_firmware.bin 0

# Verify it works
nflash info 0

# Then deploy to all
nflash flash test_firmware.bin all
```

### 2. Version Your Firmware

```bash
nflash flash my_firmware.bin all --version "1.2.3"
```

### 3. Use Capability Flags

Name your firmware descriptively so the correct capability flag is set:
- `snn_firmware.bin` → FW_CAP_SNN
- `matrix_firmware.bin` → FW_CAP_MATRIX
- `signal_firmware.bin` → FW_CAP_SIGNAL

### 4. Handle Bus Messages Gracefully

```c
void app_main(void) {
    while (1) {
        uint8_t buffer[256];
        uint16_t len;
        
        if (api->bus_receive(buffer, sizeof(buffer), &len)) {
            if (len > 0 && len <= sizeof(buffer)) {
                handle_message(buffer, len);
            }
        }
    }
}
```

### 5. Implement Graceful Shutdown

```c
// Listen for shutdown command
if (cmd == CMD_SHUTDOWN) {
    // Save state to flash
    api->flash_write(USER_DATA_BASE, &state, sizeof(state));
    
    // Send ACK
    api->bus_send(node_id, "ACK", 3);
    
    // Reboot
    api->system_reset();
}
```

## Troubleshooting

### Firmware Won't Boot

**Check:**
1. Is the firmware size < 112KB?
2. Is the entry point correct in the linker script?
3. Does the vector table have the correct stack pointer?

**Debug:**
```bash
# Check firmware info
nflash info 0

# If no valid firmware, bootloader is running
# Re-flash with --no-reboot to stay in bootloader mode
nflash flash firmware.bin 0 --no-reboot
```

### Node Not Responding After Flash

**Likely Cause:** Firmware crashed or has a bug

**Solution:**
1. Node is stuck in bootloader mode (safe)
2. Flash a known-good firmware
3. Or flash the default SNN firmware

```bash
nflash flash snn_firmware.bin 0
```

### CRC Mismatch Error

**Cause:** Firmware file corrupted during transfer

**Solution:**
```bash
# Re-flash
nflash flash firmware.bin 0
```

## Conclusion

The Z1 bootloader architecture gives you the flexibility to run any custom algorithm on the compute nodes while maintaining safety and ease of updates. Start with the provided template, build your firmware, and deploy it across hundreds of nodes with a single command.

Happy firmware development!
