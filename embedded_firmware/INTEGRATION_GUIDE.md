# Z1 Firmware Integration Guide

**Date:** November 12, 2025  
**Purpose:** Complete guide for integrating and building Z1 cluster firmware  
**Status:** ✅ All missing implementations complete

---

## Overview

This guide explains how to build, flash, and integrate the complete Z1 neuromorphic cluster firmware, including the newly implemented SNN execution engine.

---

## What's Been Implemented

### ✅ Node Firmware - Complete SNN Engine

**File:** `node/z1_snn_engine.c` (600+ lines)

**Features:**
- Neuron table parsing from PSRAM (0x20100000)
- LIF (Leaky Integrate-and-Fire) neuron model
- Spike queue management
- Multi-neuron spike processing
- Z1 bus integration for inter-node communication
- Statistics tracking
- Debug output

**Key Functions:**
```c
bool z1_snn_engine_init(void);
bool z1_snn_engine_load_network(void);
bool z1_snn_engine_start(void);
void z1_snn_engine_stop(void);
void z1_snn_engine_step(void);
bool z1_snn_engine_inject_spike(uint16_t neuron_id, float value);
void z1_snn_handle_bus_command(z1_bus_message_t* msg);
```

---

### ✅ Controller Firmware - Complete SNN Endpoints

**File:** `controller/z1_http_api.c` (added 200+ lines)

**New Endpoints:**
1. **POST /api/snn/deploy** - Deploy neuron tables to nodes
2. **POST /api/snn/input** - Inject spikes into neurons
3. **GET /api/snn/events** - Retrieve spike events

**Features:**
- Binary neuron table deployment
- Chunked PSRAM writes (256 bytes per message)
- Multi-node spike injection
- Network status tracking

---

## Build Instructions

### Prerequisites

1. **Pico SDK** - Version 1.5.0 or later
   ```bash
   git clone https://github.com/raspberrypi/pico-sdk.git
   cd pico-sdk
   git submodule update --init
   export PICO_SDK_PATH=$(pwd)
   ```

2. **ARM GCC Toolchain**
   ```bash
   sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
   ```

3. **CMake** - Version 3.13 or later
   ```bash
   sudo apt install cmake
   ```

---

### Building Node Firmware

```bash
cd embedded_firmware/node
mkdir build
cd build
cmake ..
make -j4
```

**Output:** `z1_node.uf2` - Ready to flash to RP2350B

---

### Building Controller Firmware

```bash
cd embedded_firmware/controller
mkdir build
cd build
cmake ..
make -j4
```

**Output:** `z1_controller.uf2` - Ready to flash to controller RP2350B

---

### Building Bootloader

```bash
cd embedded_firmware/bootloader
mkdir build
cd build
cmake ..
make -j4
```

**Output:** `z1_bootloader.uf2` - Flash first (occupies 0x10000000-0x10004000)

---

## Flashing Procedure

### Step 1: Flash Bootloader (One-time)

1. Hold BOOTSEL button on RP2350B
2. Connect USB
3. Copy `z1_bootloader.uf2` to mounted drive
4. Device reboots into bootloader

### Step 2: Flash Node Firmware

**Via Bootloader (Network):**
```bash
nflash flash z1_node.bin <node_id>
```

**Via USB (Direct):**
1. Hold BOOTSEL button
2. Connect USB
3. Copy `z1_node.uf2` to mounted drive

### Step 3: Flash Controller Firmware

Same as node firmware, but use `z1_controller.uf2`

---

## Integration with Existing Code

### Node Main Loop Integration

**File:** `node/node.c`

Add to main loop:
```c
#include "z1_snn_engine.h"

int main() {
    // Existing initialization
    stdio_init_all();
    psram_init();
    z1_bus_init(node_id);
    
    // Initialize SNN engine
    z1_snn_engine_init();
    
    while (1) {
        // Check for Z1 bus messages
        z1_bus_message_t msg;
        if (z1_bus_receive(&msg)) {
            // Handle SNN commands
            if (msg.command >= Z1_CMD_SNN_LOAD_TABLE && 
                msg.command <= Z1_CMD_SNN_GET_SPIKES) {
                z1_snn_handle_bus_command(&msg);
            }
            // Handle other commands...
        }
        
        // Run SNN engine step
        if (z1_snn_engine_is_running()) {
            z1_snn_engine_step();
        }
        
        // Existing main loop code...
        sleep_ms(1);
    }
}
```

---

### Controller HTTP Server Integration

**File:** `controller/z1_http_api.c`

Add route handling:
```c
void z1_http_handle_request(http_request_t* req) {
    if (strcmp(req->method, "POST") == 0) {
        if (strcmp(req->path, "/api/snn/deploy") == 0) {
            handle_post_snn_deploy(conn, req->body, req->body_length);
        }
        else if (strcmp(req->path, "/api/snn/input") == 0) {
            handle_post_snn_inject(conn, req->body, req->body_length);
        }
        else if (strcmp(req->path, "/api/snn/start") == 0) {
            handle_post_snn_start(conn);
        }
        else if (strcmp(req->path, "/api/snn/stop") == 0) {
            handle_post_snn_stop(conn);
        }
    }
    else if (strcmp(req->method, "GET") == 0) {
        if (strcmp(req->path, "/api/snn/status") == 0) {
            handle_get_snn_status(conn);
        }
        else if (strncmp(req->path, "/api/snn/events", 15) == 0) {
            uint16_t count = 10;  // Parse from query string
            handle_get_snn_events(conn, count);
        }
    }
}
```

---

## Memory Layout

### Node RP2350B (2MB Flash + 8MB PSRAM)

```
Flash (2MB):
0x10000000 - 0x10004000  Bootloader (16KB)
0x10004000 - 0x10020000  Application Firmware (112KB)
0x10020000 - 0x10040000  Firmware Update Buffer (128KB)
0x10040000 - 0x10200000  User Data / Reserved (1.75MB)

PSRAM (8MB):
0x20000000 - 0x20100000  General Purpose (1MB)
0x20100000 - 0x20200000  Neuron Tables (1MB, 256 neurons max)
0x20200000 - 0x20800000  Reserved (6MB)
```

### Neuron Table Format (256 bytes per neuron)

```
Offset  Size  Field
------  ----  -----
0-1     2     uint16_t neuron_id (global)
2-3     2     uint16_t synapse_count
4-7     4     float threshold
8-11    4     float leak_rate
12-15   4     uint32_t refractory_period_us
16-239  224   Synapse table (28 synapses max, 8 bytes each)
  0-1   2       uint16_t source_neuron_id (global)
  2-5   4       float weight
  6-7   2       uint16_t delay_us
240-255 16    Reserved
```

---

## Testing

### Unit Tests

**Test 1: Neuron Table Parsing**
```c
void test_neuron_parsing() {
    uint8_t test_entry[256] = {0};
    
    // Neuron 5, threshold 1.0, 2 synapses
    *(uint16_t*)(test_entry + 0) = 5;
    *(uint16_t*)(test_entry + 2) = 2;
    *(float*)(test_entry + 4) = 1.0f;
    *(float*)(test_entry + 8) = 0.9f;
    
    // Synapse 0: from neuron 0, weight 0.5
    *(uint16_t*)(test_entry + 16) = 0;
    *(float*)(test_entry + 18) = 0.5f;
    
    // Synapse 1: from neuron 1, weight 0.7
    *(uint16_t*)(test_entry + 24) = 1;
    *(float*)(test_entry + 26) = 0.7f;
    
    z1_neuron_t neuron;
    assert(parse_neuron_entry(test_entry, &neuron));
    assert(neuron.neuron_id == 5);
    assert(neuron.synapse_count == 2);
    assert(neuron.threshold == 1.0f);
}
```

**Test 2: Spike Processing**
```c
void test_spike_processing() {
    z1_snn_engine_init();
    // Load test network...
    z1_snn_engine_start();
    
    // Inject spike
    z1_snn_engine_inject_spike(0, 1.0f);
    
    // Run simulation
    for (int i = 0; i < 100; i++) {
        z1_snn_engine_step();
    }
    
    // Check statistics
    z1_snn_stats_t stats;
    z1_snn_engine_get_stats(&stats);
    assert(stats.spikes_processed > 0);
}
```

---

### Integration Tests

**Test 1: Simple 2-Neuron Network**

1. Deploy 2-neuron network:
   - Neuron 0: Input (threshold 1.0)
   - Neuron 1: Output (threshold 1.0, synapse from 0, weight 1.5)

2. Inject spike into neuron 0
3. Verify neuron 1 fires after propagation

**Test 2: XOR Network**

1. Deploy 7-neuron XOR network
2. Test all 4 input patterns
3. Verify correct outputs

---

## Debugging

### Enable Debug Output

**Node firmware:**
```c
#define SNN_DEBUG 1

// In z1_snn_engine.c, add:
#if SNN_DEBUG
    printf("[SNN] Neuron %u fired! V_mem=%.3f\n", neuron->neuron_id, neuron->membrane_potential);
#endif
```

**View debug output:**
```bash
# Via USB serial
screen /dev/ttyACM0 115200

# Or via network (if implemented)
curl http://192.168.1.222/api/node/0/logs
```

### Common Issues

**Issue 1: Neuron table not loading**
- Check PSRAM initialization
- Verify neuron table was written to 0x20100000
- Check for parsing errors in debug output

**Issue 2: No spikes generated**
- Verify weights are strong enough to reach threshold
- Check leak rate (high leak prevents accumulation)
- Ensure refractory period isn't too long

**Issue 3: Bus communication failures**
- Check Z1 bus initialization
- Verify node IDs are correct
- Check bus timing and clock configuration

---

## Performance Optimization

### 1. Fixed-Point Math

Replace floating-point with Q16.16 fixed-point:
```c
typedef int32_t fixed_t;  // Q16.16 format

#define FLOAT_TO_FIXED(f) ((fixed_t)((f) * 65536.0f))
#define FIXED_TO_FLOAT(x) ((float)(x) / 65536.0f)
#define FIXED_MUL(a, b) (((int64_t)(a) * (b)) >> 16)
```

**Benefit:** 5-10x faster on RP2350B

### 2. Lookup Tables

Pre-compute exponential decay:
```c
static float leak_table[256];  // Pre-computed exp(-t/tau)

void init_leak_table() {
    for (int i = 0; i < 256; i++) {
        float t = i * 1000.0f;  // microseconds
        leak_table[i] = expf(-t / 1000000.0f);
    }
}
```

### 3. DMA for PSRAM Access

Use DMA for bulk neuron table reads:
```c
void psram_read_dma(uint32_t addr, uint8_t* buffer, size_t length) {
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    
    dma_channel_configure(dma_chan, &c, buffer, (void*)addr, length, true);
    dma_channel_wait_for_finish_blocking(dma_chan);
}
```

---

## Deployment Workflow

### Full System Deployment

```bash
# 1. Build all firmware
cd embedded_firmware
./build_all.sh

# 2. Flash bootloaders (one-time)
for node in {0..15}; do
    nflash flash bootloader/build/z1_bootloader.bin $node
done

# 3. Flash node firmware
for node in {0..15}; do
    nflash flash node/build/z1_node.bin $node
done

# 4. Flash controller firmware
nflash flash controller/build/z1_controller.bin controller

# 5. Deploy SNN
nsnn deploy examples/xor_snn.json

# 6. Start execution
nsnn start

# 7. Monitor
nsnn monitor 5000
```

---

## Next Steps

### Immediate

1. ✅ **Build and test node firmware** - Verify compilation
2. ✅ **Build and test controller firmware** - Verify HTTP endpoints
3. ⚠️ **Integration testing** - Test on real hardware

### Short-term

4. ⚠️ **Performance optimization** - Fixed-point math, DMA
5. ⚠️ **STDP implementation** - Port from emulator
6. ⚠️ **Comprehensive testing** - Unit and integration tests

### Long-term

7. ⚠️ **Advanced features** - Homeostatic plasticity, structural plasticity
8. ⚠️ **Visualization tools** - Real-time network visualization
9. ⚠️ **Production hardening** - Error recovery, watchdogs

---

## Support

### Documentation
- `FIRMWARE_AUDIT_REPORT.md` - Audit results
- `STDP_IMPLEMENTATION.md` - STDP learning guide
- `docs/firmware_development_guide.md` - Firmware development

### Code Examples
- `emulator/core/snn_engine.py` - Reference implementation
- `emulator/examples/` - Network topologies

### Contact
- GitHub Issues: https://github.com/ahtx/neurofab-z1-cluster/issues
- Documentation: https://github.com/ahtx/neurofab-z1-cluster

---

**Integration Guide Version:** 1.0  
**Last Updated:** November 12, 2025  
**Status:** ✅ Ready for hardware testing
