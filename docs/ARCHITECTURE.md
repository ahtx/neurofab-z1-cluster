# Z1 Neuromorphic Cluster - System Architecture

**Version:** 3.0 (Production Ready)  
**Date:** November 13, 2025  
**Firmware:** Controller v3.0, Node v2.1  
**Status:** ✅ QA Verified

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Firmware Architecture](#firmware-architecture)
4. [Communication Protocols](#communication-protocols)
5. [SNN Execution Engine](#snn-execution-engine)
6. [Memory Management](#memory-management)
7. [Data Structures](#data-structures)
8. [Software Tools](#software-tools)

---

## Overview

The Z1 Neuromorphic Cluster is a distributed computing system designed for real-time Spiking Neural Network (SNN) execution. The system consists of up to 16 compute nodes connected via a parallel matrix bus, with a controller node providing HTTP API access and cluster management.

### Design Principles

- **Distributed Execution:** SNNs distributed across multiple nodes for scalability
- **Event-Driven:** Asynchronous spike-based communication
- **Low Latency:** Parallel matrix bus for fast inter-node communication
- **Scalable:** Up to 16,384 neurons per cluster (1024 per node × 16 nodes)
- **Real-Time:** 1ms simulation timestep (1000 Hz update rate)

### System Layers

```
┌─────────────────────────────────────────────────────┐
│  Layer 1: Python CLI Tools (User Workstation)      │
│  - nls, nping, ndeploy, nstart, nstop, ninject     │
└──────────────────┬──────────────────────────────────┘
                   │ HTTP REST API (Ethernet)
┌──────────────────▼──────────────────────────────────┐
│  Layer 2: Controller Node (RP2350B + W5500)        │
│  - HTTP Server, API Handlers, Bus Master           │
└──────────────────┬──────────────────────────────────┘
                   │ Matrix Bus (16-bit parallel)
┌──────────────────▼──────────────────────────────────┐
│  Layer 3: Compute Nodes (RP2350B + PSRAM)          │
│  - SNN Engine, Neuron Cache, Spike Router          │
└─────────────────────────────────────────────────────┘
```

---

## Hardware Architecture

### Controller Node

**Microcontroller:** Raspberry Pi RP2350B
- Dual Cortex-M33 cores @ 133 MHz
- 520 KB SRAM
- Pico SDK 2.0.0

**Peripherals:**
- **W5500 Ethernet Controller** (SPI)
  - Hardware TCP/IP stack
  - 10/100 Mbps Ethernet
  - 8 socket channels
  - Pins: MISO=GPIO16, MOSI=GPIO19, SCK=GPIO18, CS=GPIO17
  
- **SSD1306 OLED Display** (I2C, optional)
  - 128×64 pixels
  - Real-time status display
  - Pins: SDA=GPIO4, SCL=GPIO5
  
- **APS6404L PSRAM** (QSPI)
  - 8 MB external memory
  - Memory-mapped access
  - Used for buffers and caching

- **Matrix Bus** (GPIO0-15)
  - 16-bit parallel data bus
  - Bidirectional communication
  - Collision detection and ACK

### Compute Node

**Microcontroller:** Raspberry Pi RP2350B
- Same specs as controller

**Peripherals:**
- **APS6404L PSRAM** (QSPI)
  - 8 MB external memory
  - Neuron table storage (256 bytes per neuron)
  - 1024 neurons = 256 KB allocation
  
- **Matrix Bus** (GPIO0-15)
  - Slave mode with interrupt handling
  - Receives commands and spikes
  - Sends responses and spikes
  
- **Node ID Strapping** (GPIO40-43)
  - Hardware-configured unique ID (0-15)
  - Read at boot time
  - Pull-up/pull-down resistors
  
- **RGB LED** (GPIO44-46, optional)
  - PWM-controlled status indicator
  - Boot sequence: RED → GREEN → BLUE
  - Activity indication during operation

### Interconnect

**Matrix Bus Specifications:**
- **Width:** 16 bits (GPIO0-15)
- **Topology:** Shared parallel bus (all nodes connected)
- **Protocol:** Command/data with ACK
- **Speed:** ~100 kHz effective (10 μs per byte)
- **Collision Handling:** Exponential backoff
- **Maximum Nodes:** 16 (1 controller + 15 compute)

**Physical Implementation:**
- Ribbon cable or PCB traces
- Common ground required
- Pull-up/pull-down resistors on data lines
- Maximum bus length: ~30 cm (signal integrity)

---

## Firmware Architecture

### Controller Firmware (120 KB)

**Main Components:**

1. **controller_main.c** - Entry point and initialization
   - System initialization
   - W5500 Ethernet setup
   - OLED display initialization
   - Matrix bus initialization
   - Node discovery on boot

2. **w5500_http_server.c** - HTTP server implementation
   - Socket management (port 80)
   - Request parsing
   - Routing to API handlers
   - Response generation
   - Chunked transfer encoding

3. **z1_http_api.c** - API endpoint handlers (21 endpoints)
   - Node management (list, ping, reset)
   - SNN operations (deploy, start, stop, inject)
   - Memory access (read, write)
   - Firmware management (upload, verify, install)

4. **z1_matrix_bus.c** - Matrix bus driver
   - Bus master controller
   - Command transmission
   - Response reception
   - Collision detection
   - Ping history tracking

5. **z1_multiframe.c** - Multi-frame protocol
   - Large data transfer (>254 bytes)
   - Frame assembly: START → DATA → END
   - Timeout and retry logic
   - CRC validation (optional)

6. **z1_display.c** - OLED display driver
   - SSD1306 I2C communication
   - Status message rendering
   - Spike statistics display
   - Error indication

**Memory Layout:**
- Code: 573 KB (Flash)
- Data: 31 KB (SRAM)
- PSRAM: 8 MB (buffers, cache)

**Boot Sequence:**
1. Initialize stdio (USB serial)
2. Initialize LED
3. Initialize OLED display
4. Initialize PSRAM
5. Initialize matrix bus
6. Discover nodes (ping 0-15)
7. Initialize W5500 Ethernet
8. Setup TCP server (port 80)
9. Enter main loop (HTTP server)

### Node Firmware (89 KB)

**Main Components:**

1. **node.c** - Entry point and command processing
   - System initialization
   - Node ID detection (GPIO40-43)
   - Matrix bus interrupt handling
   - Command dispatcher
   - Main loop (SNN step + bus handling)

2. **z1_snn_engine_v2.c** - SNN execution engine
   - Timestep simulation (1ms)
   - Neuron processing (LIF dynamics)
   - Spike generation and routing
   - Spike queue management
   - Synapse traversal

3. **z1_psram_neurons.c** - PSRAM neuron table management
   - Neuron table allocation (256 bytes per neuron)
   - Load/store operations
   - Table initialization
   - Metadata management

4. **z1_neuron_cache.c** - LRU neuron cache
   - 8-entry cache (2 KB)
   - Least Recently Used replacement
   - Dirty bit tracking
   - Writeback on eviction
   - Cache flush on stop

5. **z1_matrix_bus.c** - Matrix bus driver
   - Bus slave with interrupt
   - Command reception
   - Response transmission
   - ACK protocol
   - Collision avoidance

6. **z1_multiframe.c** - Multi-frame protocol
   - Frame reception and assembly
   - Buffer management
   - Callback on completion
   - Error handling

7. **psram_rp2350.c** - PSRAM driver
   - QSPI initialization
   - Memory-mapped access
   - Read/write operations
   - Address validation

**Memory Layout:**
- Code: 541 KB (Flash)
- Data: 37 KB (SRAM, includes 23 KB cache)
- PSRAM: 8 MB (neuron tables)

**Boot Sequence:**
1. Initialize stdio (USB serial)
2. Initialize GPIO inputs
3. Read node ID (GPIO40-43)
4. Initialize RGB LED
5. Initialize matrix bus
6. Initialize PSRAM
7. Initialize multi-frame receiver
8. Initialize SNN engine
9. Initialize neuron cache
10. Enter main loop (bus + SNN step)

**Main Loop:**
```c
while (1) {
    z1_bus_handle_interrupt();  // Process bus commands
    z1_snn_step(current_time);  // Execute SNN timestep
    
    if (ping_response_pending) {
        z1_bus_write(Z1_CMD_PING_RESPONSE);
        ping_response_pending = false;
    }
}
```

---

## Communication Protocols

### HTTP REST API

**Base URL:** `http://192.168.1.222/api`

**Transport:** TCP/IP over Ethernet (W5500)

**Format:** JSON request/response

**Endpoints:** 21 total (15 fully functional, 6 with placeholders)

See [API_REFERENCE.md](API_REFERENCE.md) for complete documentation.

### Matrix Bus Protocol

**Physical Layer:**
- 16-bit parallel bus (GPIO0-15)
- Shared bus topology
- Active-high signaling

**Data Link Layer:**

**Basic Frame Format:**
```
[SYNC][NODE_ID][COMMAND][DATA...]
```

**Fields:**
- `SYNC` (8 bits): 0xAA (synchronization byte)
- `NODE_ID` (8 bits): Target node ID (0-15)
- `COMMAND` (8 bits): Command code
- `DATA` (variable): Command-specific data

**Command Set:**

| Command | Value | Description | Data |
|---------|-------|-------------|------|
| Z1_CMD_PING | 0x01 | Ping node | None |
| Z1_CMD_PING_RESPONSE | 0x02 | Ping response | 0x42 |
| Z1_CMD_RESET | 0x03 | Reset node | None |
| Z1_CMD_LED_SET | 0x10 | Set LED color | R, G, B |
| Z1_CMD_MEM_READ | 0x20 | Read memory | addr[4], len[2] |
| Z1_CMD_MEM_WRITE | 0x21 | Write memory | addr[4], data[n] |
| Z1_CMD_SNN_LOAD_TABLE | 0x30 | Load neuron table | None |
| Z1_CMD_SNN_START | 0x31 | Start SNN | None |
| Z1_CMD_SNN_STOP | 0x32 | Stop SNN | None |
| Z1_CMD_SNN_SPIKE | 0x33 | Spike event | global_id[4], time[4], flags[1] |
| Z1_CMD_FRAME_START | 0xF0 | Multi-frame start | total_length[2] |
| Z1_CMD_FRAME_DATA | 0xF1 | Multi-frame data | data[254] |
| Z1_CMD_FRAME_END | 0xF2 | Multi-frame end | CRC[2] |

**ACK Protocol:**
1. Sender writes frame to bus
2. Sender waits for ACK (timeout: 10ms)
3. Receiver processes command
4. Receiver sends ACK (0x06) or NAK (0x15)
5. Sender retries on NAK or timeout (max 3 attempts)

**Collision Detection:**
1. Node waits random backoff (0-15 ms)
2. Node checks bus idle
3. Node writes frame
4. Node verifies data on bus matches sent data
5. If mismatch, collision detected → exponential backoff

### Multi-Frame Protocol

For data >254 bytes (e.g., neuron tables, firmware):

**Transmission:**
```
1. FRAME_START: [0xF0][total_length_high][total_length_low]
2. FRAME_DATA:  [0xF1][data[0..253]]
3. FRAME_DATA:  [0xF1][data[254..507]]
   ...
4. FRAME_END:   [0xF2][CRC_high][CRC_low]
```

**Reception:**
- Buffer frames in SRAM
- Validate CRC on FRAME_END
- Invoke callback with complete data
- Clear buffer for next transfer

**Example: Deploy 256-byte neuron to node 0:**
```
1. FRAME_START: [0xF0][0x01][0x00] (256 bytes)
2. FRAME_DATA:  [0xF1][neuron_data[0..253]]
3. FRAME_DATA:  [0xF1][neuron_data[254..255]][padding]
4. FRAME_END:   [0xF2][CRC_high][CRC_low]
```

---

## SNN Execution Engine

### Neuron Model: Leaky Integrate-and-Fire (LIF)

**State Variables:**
- `membrane_potential` (float): Current membrane voltage
- `threshold` (float): Spike threshold
- `leak_rate` (float): Exponential decay constant
- `refractory_period_us` (uint32): Refractory period in microseconds
- `last_spike_time_us` (uint32): Timestamp of last spike

**Dynamics:**

1. **Leak (exponential decay):**
   ```c
   float dt = (current_time - last_update_time) / 1000.0f;  // Convert to seconds
   membrane_potential *= expf(-leak_rate * dt);
   ```

2. **Spike reception (add to potential):**
   ```c
   membrane_potential += spike_value * synapse_weight;
   ```

3. **Threshold check:**
   ```c
   if (membrane_potential >= threshold && !in_refractory) {
       generate_spike();
       membrane_potential = 0.0f;
       last_spike_time = current_time;
   }
   ```

4. **Refractory period:**
   ```c
   if (current_time - last_spike_time < refractory_period_us) {
       in_refractory = true;
   }
   ```

### Spike Routing

**Local Spike (same node):**
1. Neuron fires (potential ≥ threshold)
2. Traverse synapse list
3. For each synapse with local target:
   - Extract target neuron ID
   - Load target neuron from cache
   - Add weight to target's membrane potential
   - Mark target as dirty (writeback needed)

**Remote Spike (different node):**
1. Neuron fires
2. Traverse synapse list
3. For each synapse with remote target:
   - Extract target global ID (bits 16-23 = node, bits 0-15 = local ID)
   - Pack spike message: [global_id:4][timestamp:4][flags:1]
   - Send via multi-frame protocol to target node
4. Target node receives spike:
   - Unpack global ID
   - Extract local neuron ID
   - Load neuron from cache
   - Add 1.0 to membrane potential
   - Mark as dirty

### Spike Queue

**Purpose:** Buffer spikes for routing

**Implementation:**
- Circular buffer (128 entries)
- Each entry: `{global_neuron_id, timestamp, value}`
- Producer: `process_neuron()` on spike generation
- Consumer: `z1_snn_engine_step()` main loop

**Processing:**
```c
while (!spike_queue_empty()) {
    spike_t spike = spike_queue_pop();
    
    uint8_t target_node = (spike.global_id >> 16) & 0xFF;
    uint16_t local_id = spike.global_id & 0xFFFF;
    
    if (target_node == my_node_id) {
        // Local spike
        apply_spike_to_neuron(local_id, spike.value);
    } else {
        // Remote spike
        send_spike_via_bus(target_node, spike);
    }
}
```

### STDP Learning

**Spike-Timing-Dependent Plasticity:**

**Weight Update Rule:**
```c
if (post_spike_time > pre_spike_time) {
    // Potentiation (strengthen synapse)
    delta_t = post_spike_time - pre_spike_time;
    weight += A_plus * exp(-delta_t / tau_plus);
} else {
    // Depression (weaken synapse)
    delta_t = pre_spike_time - post_spike_time;
    weight -= A_minus * exp(-delta_t / tau_minus);
}
weight = clamp(weight, 0.0, 1.0);
```

**Parameters:**
- `A_plus` = 0.01 (potentiation amplitude)
- `A_minus` = 0.01 (depression amplitude)
- `tau_plus` = 20 ms (potentiation time constant)
- `tau_minus` = 20 ms (depression time constant)

**Implementation:**
- Weight updates occur on spike events
- Synapse weights stored in neuron table
- Writeback to PSRAM on cache eviction

---

## Memory Management

### PSRAM Allocation

**Total:** 8 MB per node

**Neuron Table:**
- **Base Address:** 0x00000000 (PSRAM start)
- **Entry Size:** 256 bytes per neuron
- **Capacity:** 1024 neurons = 256 KB
- **Format:** See [Data Structures](#data-structures)

**Remaining Space:**
- Available for future use (7.75 MB)
- Potential uses: spike history, weight snapshots, firmware buffer

### Neuron Cache

**Purpose:** Reduce PSRAM access latency

**Implementation:**
- **Size:** 8 entries (2 KB SRAM)
- **Policy:** Least Recently Used (LRU)
- **Entry:** Full 256-byte neuron structure
- **Dirty Bit:** Track modifications for writeback

**Operations:**

1. **Cache Get:**
   ```c
   neuron_t* z1_neuron_cache_get(uint16_t neuron_id) {
       // Check if in cache
       for (int i = 0; i < 8; i++) {
           if (cache[i].valid && cache[i].neuron_id == neuron_id) {
               cache[i].last_access = current_time;
               return &cache[i].neuron;
           }
       }
       
       // Cache miss: load from PSRAM
       int victim = find_lru_entry();
       if (cache[victim].dirty) {
           writeback_to_psram(victim);
       }
       load_from_psram(neuron_id, victim);
       return &cache[victim].neuron;
   }
   ```

2. **Cache Mark Dirty:**
   ```c
   void z1_neuron_cache_mark_dirty(uint16_t neuron_id) {
       for (int i = 0; i < 8; i++) {
           if (cache[i].valid && cache[i].neuron_id == neuron_id) {
               cache[i].dirty = true;
               return;
           }
       }
   }
   ```

3. **Cache Flush:**
   ```c
   void z1_neuron_cache_flush_all(void) {
       for (int i = 0; i < 8; i++) {
           if (cache[i].valid && cache[i].dirty) {
               writeback_to_psram(i);
               cache[i].dirty = false;
           }
       }
   }
   ```

**Performance:**
- Cache hit: ~10 CPU cycles
- Cache miss: ~1000 CPU cycles (PSRAM access)
- Typical hit rate: 80-90% (locality of reference)

---

## Data Structures

### Neuron Table Entry (256 bytes)

```c
typedef struct {
    // Neuron state (32 bytes)
    uint16_t neuron_id;              // Local neuron ID (0-1023)
    uint16_t flags;                  // Status flags
    float membrane_potential;        // Current potential
    float threshold;                 // Spike threshold
    float leak_rate;                 // Exponential decay rate
    uint32_t refractory_period_us;   // Refractory period
    uint32_t last_spike_time_us;     // Last spike timestamp
    uint32_t spike_count;            // Total spikes generated
    uint32_t reserved[2];            // Future use
    
    // Synapse table (224 bytes = 56 synapses × 4 bytes)
    uint16_t synapse_count;          // Number of synapses
    uint16_t synapse_capacity;       // Max synapses (56)
    struct {
        uint32_t source_global_id : 24;  // Source neuron global ID
        uint32_t weight_fixed : 8;       // Weight (0-255, scale to 0.0-1.0)
    } synapses[56];
} neuron_entry_t;
```

**Total:** 256 bytes (aligned for PSRAM access)

### Spike Message (9 bytes)

```c
typedef struct {
    uint32_t global_neuron_id;    // Source neuron (node:8 + local:16)
    uint32_t timestamp_us;        // Spike timestamp
    uint8_t flags;                // Reserved flags
} spike_message_t;
```

**Encoding:**
- Bits [31:24]: Source node ID
- Bits [23:16]: Reserved
- Bits [15:0]: Local neuron ID

### Multi-Frame Buffer

```c
typedef struct {
    uint8_t buffer[4096];         // Data buffer
    uint16_t total_length;        // Expected total length
    uint16_t received_length;     // Bytes received so far
    bool active;                  // Transfer in progress
    uint32_t start_time_ms;       // Transfer start time
} multiframe_buffer_t;
```

---

## Software Tools

### Python CLI Utilities

**Location:** `utilities/`

**Implementation:** Python 3.7+ with `requests` library

**Tools:**

1. **nls.py** - List nodes
   - Sends POST /api/nodes/discover
   - Displays active nodes

2. **nping.py** - Ping node
   - Sends POST /api/nodes/{id}/ping
   - Displays round-trip time

3. **ndeploy.py** - Deploy SNN
   - Parses JSON network definition
   - Converts to binary neuron tables
   - Sends POST /api/snn/deploy with binary data

4. **nstart.py** - Start SNN execution
   - Sends POST /api/snn/start

5. **nstop.py** - Stop SNN execution
   - Sends POST /api/snn/stop

6. **ninject.py** - Inject spike
   - Sends POST /api/snn/input with JSON spike data

**Common Code:**
- HTTP client wrapper
- Error handling
- Response parsing
- Environment variable support (Z1_CONTROLLER_IP)

---

## Performance Characteristics

### Timing

| Operation | Latency | Notes |
|-----------|---------|-------|
| Matrix bus byte transfer | 10 μs | Single byte write + ACK |
| Node ping round-trip | 12 ms | Includes bus overhead |
| Neuron cache hit | 0.1 μs | SRAM access |
| Neuron cache miss | 10 μs | PSRAM load |
| SNN timestep (1024 neurons) | 1 ms | 1000 Hz simulation rate |
| Inter-node spike routing | 50 μs | Multi-frame transfer |
| HTTP request processing | 10-50 ms | Depends on operation |

### Throughput

| Metric | Value | Notes |
|--------|-------|-------|
| Matrix bus bandwidth | 100 KB/s | Effective throughput |
| Ethernet bandwidth | 10 Mbps | W5500 hardware limit |
| Spikes per second (local) | 100,000+ | Limited by neuron processing |
| Spikes per second (remote) | 2,000 | Limited by bus bandwidth |
| Neurons per node | 1,024 | PSRAM allocation limit |
| Total cluster capacity | 16,384 | 16 nodes × 1024 neurons |

### Memory Usage

| Component | Controller | Node | Notes |
|-----------|------------|------|-------|
| Firmware (Flash) | 573 KB | 541 KB | Code + constants |
| SRAM | 31 KB | 37 KB | Stack + heap + cache |
| PSRAM | 8 MB | 8 MB | Buffers / neuron tables |

---

## Future Enhancements

### Planned Features

1. **Output Spike Collection** - Capture and return output spikes via API
2. **Firmware Over-The-Air** - Remote firmware updates via HTTP
3. **Memory Read Response Parsing** - Complete memory read implementation
4. **Multi-Backplane Support** - Scale beyond 16 nodes
5. **Authentication** - API key or token-based security
6. **WebSocket Support** - Real-time spike streaming
7. **Performance Monitoring** - Cache hit rate, bus utilization metrics

### Scalability Considerations

**Current Limits:**
- 16 nodes per cluster (matrix bus addressing)
- 1024 neurons per node (PSRAM allocation)
- 16,384 total neurons per cluster

**Scaling Options:**
- **Hierarchical Bus:** Multiple backplanes with gateway nodes
- **Ethernet Interconnect:** Replace matrix bus with Ethernet for long-distance
- **Larger PSRAM:** 16 MB or 32 MB chips for more neurons per node
- **Dual-Core Utilization:** Use second RP2350B core for parallel processing

---

## References

### Hardware Documentation

- [RP2350B Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [W5500 Datasheet](https://www.wiznet.io/product-item/w5500/)
- [APS6404L PSRAM Datasheet](https://www.apmemory.com/products/psram/)
- [SSD1306 OLED Datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)

### Firmware Source

- `embedded_firmware/controller/` - Controller firmware source
- `embedded_firmware/node/` - Node firmware source
- `embedded_firmware/common/` - Shared protocol definitions

### Related Documentation

- [USER_GUIDE.md](USER_GUIDE.md) - Hardware setup and usage
- [API_REFERENCE.md](API_REFERENCE.md) - Complete API documentation
- [SNN_GUIDE.md](SNN_GUIDE.md) - SNN implementation details
- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - Firmware development
- [CODE_WALKTHROUGH.md](CODE_WALKTHROUGH.md) - Source code tour
- [QA_VERIFICATION_REPORT.md](../QA_VERIFICATION_REPORT.md) - QA verification

---

**Version:** 3.0  
**Last Updated:** November 13, 2025  
**Status:** Production Ready
