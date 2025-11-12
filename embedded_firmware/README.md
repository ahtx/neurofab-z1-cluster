# NeuroFab Z1 Embedded Firmware

Embedded C firmware for NeuroFab Z1 neuromorphic cluster nodes.

## Directory Structure

```
neurofab-embedded/
├── common/           # Shared code for all nodes
│   └── z1_protocol_extended.h   # Extended bus protocol definitions
├── controller/       # Controller node code
│   ├── z1_http_api.h            # HTTP REST API server header
│   └── z1_http_api.c            # HTTP REST API implementation
└── node/             # Compute node code
    └── z1_snn_engine.h          # SNN execution engine
```

## Architecture

The Z1 cluster consists of two types of nodes:

### Controller Node

The controller node runs an HTTP REST API server that accepts commands from external clients (Python utilities) and translates them into Z1 bus operations. It manages cluster-wide operations like node discovery, SNN deployment, and monitoring.

**Key Components:**
- HTTP server on W5500 Ethernet controller
- RESTful API endpoints for node and SNN management
- JSON request/response handling
- Base64 encoding for binary data transfer
- Multi-frame bus protocol for large transfers

### Compute Nodes

Compute nodes execute the SNN workload. Each node maintains a table of neurons in PSRAM and processes incoming spike messages from the bus.

**Key Components:**
- Leaky Integrate-and-Fire (LIF) neuron model
- Spike queue for buffering incoming spikes
- PSRAM-based neuron table storage
- Bus-based spike communication
- Weight update support for online learning

## Extended Bus Protocol

The extended protocol adds new commands beyond the basic LED control:

### Memory Operations (0x50-0x5F)
- `MEM_READ_REQ` (0x50) - Request memory read
- `MEM_READ_DATA` (0x51) - Memory read response
- `MEM_WRITE` (0x52) - Write memory
- `MEM_WRITE_ACK` (0x53) - Write acknowledgment

### Code Execution (0x60-0x6F)
- `EXEC_CODE` (0x60) - Execute code at address
- `RESET_NODE` (0x63) - Reset node

### SNN Operations (0x70-0x7F)
- `SNN_SPIKE` (0x70) - Neuron spike event
- `SNN_WEIGHT_UPDATE` (0x71) - Update synapse weight
- `SNN_LOAD_TABLE` (0x72) - Load neuron table
- `SNN_START` (0x73) - Start SNN execution
- `SNN_STOP` (0x74) - Stop SNN execution
- `SNN_QUERY_ACTIVITY` (0x75) - Query spike count

### Node Management (0x80-0x8F)
- `NODE_INFO_REQ` (0x80) - Request node information
- `NODE_INFO_RESP` (0x81) - Node information response
- `NODE_HEARTBEAT` (0x82) - Heartbeat/keepalive

## Neuron Table Format

Each neuron occupies 256 bytes in PSRAM:

```c
struct neuron_entry {
    // Neuron state (16 bytes)
    uint16_t neuron_id;
    uint16_t flags;
    float membrane_potential;
    float threshold;
    uint32_t last_spike_time_us;
    
    // Synapse metadata (8 bytes)
    uint16_t synapse_count;
    uint16_t synapse_capacity;
    uint32_t reserved1;
    
    // Parameters (8 bytes)
    float leak_rate;
    uint32_t refractory_period_us;
    
    // Reserved (8 bytes)
    uint32_t reserved2[2];
    
    // Synapses (240 bytes, 60 × 4 bytes)
    uint32_t synapses[60];  // [source_id:24][weight:8]
};
```

## Spike Message Format

Spikes are transmitted as 16-bit messages on the bus:

```
Bits [15:12]: Message type (0x7 = spike)
Bits [11:0]:  Local neuron ID (0-4095)
```

For inter-node spikes, multi-frame messages include:
- Frame 1: [0xAA][source_node_id]
- Frame 2: [SNN_SPIKE][local_neuron_id_high]
- Frame 3: [local_neuron_id_low][flags]

## Integration with Existing Code

This code extends the existing Z1 firmware:

### Controller Integration

Add to `tcp_server_test.c`:

```c
#include "z1_http_api.h"

// In main():
z1_http_api_init();

// In main loop:
z1_http_api_process();
```

### Node Integration

Add to `node.c`:

```c
#include "z1_snn_engine.h"

// In main():
z1_snn_init(Z1_NODE_ID);

// In bus command handler:
case Z1_CMD_SNN_SPIKE:
    z1_snn_process_spike(data);
    break;
case Z1_CMD_SNN_START:
    z1_snn_start();
    break;
// ... etc
```

## Building

The code is designed to integrate with the existing Pico SDK build system. Add the new files to your `CMakeLists.txt`:

```cmake
target_sources(controller PRIVATE
    tcp_server_test.c
    z1_matrix_bus.c
    z1_http_api.c
    # ... other sources
)

target_sources(node PRIVATE
    node.c
    z1_matrix_bus.c
    z1_snn_engine.c
    # ... other sources
)
```

## Memory Layout

### Controller Node
- Flash: Firmware code
- SRAM: HTTP buffers, connection state
- PSRAM: File cache, large response buffers

### Compute Node
- Flash: Firmware code
- SRAM: Active neuron cache, spike queue
- PSRAM: Neuron table (up to 4096 neurons × 256 bytes = 1MB)

## API Endpoints

See `z1_http_api.h` for complete endpoint documentation.

### Example: Deploy SNN

```http
POST /api/snn/deploy
Content-Type: application/json

{
  "network_name": "test_network",
  "neuron_count": 1000,
  "layers": [...],
  "connections": [...]
}
```

### Example: Read Node Memory

```http
GET /api/nodes/0/memory?addr=0x20000000&len=256

Response:
{
  "addr": 536870912,
  "length": 256,
  "data": "base64_encoded_data..."
}
```

## Performance Considerations

### Bus Bandwidth

Current software-driven bus: ~100μs per 16-bit transfer
PIO-optimized bus: ~50ns per 16-bit transfer (2000× faster)

For maximum performance, migrate to PIO-based bus implementation.

### Neuron Processing

With 200 MHz ARM Cortex-M33:
- ~1000 cycles per neuron update
- ~200,000 neurons/second per core
- ~400,000 neurons/second per node (dual core)

For 30,000 neurons per node: ~75ms per full network update

### Spike Routing

Broadcast spikes enable efficient one-to-many communication. Nodes filter spikes based on synapse tables, processing only relevant spikes.

Average spike processing: ~500 cycles
Spike rate: ~400,000 spikes/second per core

## Testing

### Unit Tests

Test individual components:
- Bus protocol encoding/decoding
- JSON generation
- Neuron processing
- Spike queue operations

### Integration Tests

Test end-to-end workflows:
- Node discovery
- Memory read/write
- SNN deployment
- Spike propagation

### Stress Tests

Test under load:
- High spike rates
- Large neuron tables
- Concurrent API requests
- Bus contention

## Future Enhancements

### PIO Bus Implementation

Migrate bus protocol to PIO state machines for 100-1000× performance improvement.

### Hardware Acceleration

Use RP2350 RISC-V cores for parallel neuron processing.

### Compression

Compress neuron tables for faster loading and reduced memory usage.

### STDP Learning

Implement Spike-Timing-Dependent Plasticity for online learning.

### Multi-Backplane Support

Extend protocol to support multiple backplanes networked via Ethernet.

## License

Copyright NeuroFab Corp. All rights reserved.
