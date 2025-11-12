# NeuroFab Z1 Cluster Management System Design

## Architecture Overview

The system consists of three integrated layers that enable comprehensive management and deployment of Spiking Neural Networks (SNNs) on the Z1 neuromorphic cluster.

### Layer 1: Python Cluster Management Utilities

A suite of Unix-style command-line tools provides intuitive cluster interaction. These utilities communicate with controller nodes via HTTP REST API over Ethernet, translating high-level commands into low-level bus operations. The design follows the Unix philosophy of simple, composable tools that do one thing well.

### Layer 2: Embedded HTTP Server

Each controller node runs a lightweight HTTP server on the W5500 Ethernet interface. The server exposes RESTful endpoints for node management, memory access, and SNN operations. The implementation uses the existing W5500 TCP/IP stack and extends the current tcp_server_test.c codebase.

### Layer 3: SNN Deployment Framework

A specialized framework compiles high-level SNN definitions into distributed neuron tables, manages weight distribution, coordinates spike routing, and monitors network activity. The framework leverages the Z1 bus for low-latency inter-neuron communication.

## Communication Protocol Stack

### HTTP REST API Specification

All endpoints return JSON responses with consistent error handling. Binary data transfers use chunked encoding for large memory operations.

#### Node Management Endpoints

**GET /api/nodes**
- Returns list of all discovered nodes with status
- Response: `{"nodes": [{"id": 0, "status": "active", "memory_free": 7340032}, ...]}`

**GET /api/nodes/{id}**
- Returns detailed information for specific node
- Response: `{"id": 0, "status": "active", "uptime_ms": 123456, "led_state": {"r": 0, "g": 128, "b": 0}}`

**POST /api/nodes/{id}/reset**
- Resets specified node via bus command
- Request: `{}`
- Response: `{"status": "ok", "node_id": 0}`

**GET /api/nodes/{id}/memory?addr=0x20000000&len=256**
- Reads memory range from node PSRAM
- Response: `{"addr": 536870912, "length": 256, "data": "base64_encoded_data"}`

**POST /api/nodes/{id}/memory**
- Writes data to node PSRAM
- Request: `{"addr": 536870912, "data": "base64_encoded_data"}`
- Response: `{"status": "ok", "bytes_written": 256}`

**POST /api/nodes/{id}/execute**
- Triggers code execution on node
- Request: `{"entry_point": 536870912, "params": [1, 2, 3]}`
- Response: `{"status": "ok", "execution_id": 42}`

#### SNN Management Endpoints

**POST /api/snn/deploy**
- Deploys SNN topology to cluster
- Request: JSON topology definition (see SNN Format below)
- Response: `{"status": "ok", "neurons_deployed": 30000, "nodes_used": 12}`

**GET /api/snn/topology**
- Returns current SNN topology
- Response: Complete SNN definition with node assignments

**POST /api/snn/weights**
- Updates synaptic weights
- Request: `{"updates": [{"neuron_id": 12345, "synapse_idx": 5, "weight": 0.75}, ...]}`
- Response: `{"status": "ok", "weights_updated": 100}`

**GET /api/snn/activity?duration_ms=1000**
- Captures spike activity for specified duration
- Response: `{"spikes": [{"neuron_id": 12345, "timestamp_us": 567890}, ...]}`

**POST /api/snn/input**
- Injects input spikes into network
- Request: `{"spikes": [{"neuron_id": 0, "value": 1.0}, ...]}`
- Response: `{"status": "ok", "spikes_injected": 10}`

### Extended Z1 Bus Protocol

The existing bus protocol is extended with new commands for memory operations and SNN coordination.

#### New Bus Commands

| Command | Value | Description | Data Format |
|---------|-------|-------------|-------------|
| MEM_READ_REQ | 0x50 | Request memory read | addr[31:24] |
| MEM_READ_DATA | 0x51 | Memory read response | data byte |
| MEM_WRITE | 0x52 | Write memory | addr + data |
| EXEC_CODE | 0x60 | Execute at address | addr[31:24] |
| RESET_NODE | 0x61 | Reset node | 0x00 |
| SNN_SPIKE | 0x70 | Neuron spike event | neuron_id[15:0] |
| SNN_WEIGHT_UPDATE | 0x71 | Update synapse weight | neuron_id + synapse_idx + weight |
| SNN_LOAD_TABLE | 0x72 | Load neuron table | table_offset + data |
| SNN_START | 0x73 | Start SNN execution | 0x00 |
| SNN_STOP | 0x74 | Stop SNN execution | 0x00 |
| SNN_QUERY_ACTIVITY | 0x75 | Query spike count | 0x00 |

#### Multi-Frame Transfers

For operations requiring more than 16 bits of data, multiple frames are sent sequentially. The first frame contains the command and high-order address/data bits, subsequent frames contain continuation data.

**Memory Read Example:**
1. Frame 1: [0xAA][controller_id]
2. Frame 2: [MEM_READ_REQ][addr[31:24]]
3. Frame 3: [addr[23:16]][addr[15:8]]
4. Frame 4: [addr[7:0]][length]

**Memory Write Example:**
1. Frame 1: [0xAA][controller_id]
2. Frame 2: [MEM_WRITE][addr[31:24]]
3. Frame 3: [addr[23:16]][addr[15:8]]
4. Frame 4: [addr[7:0]][data[7:0]]

### SNN Message Format

#### Spike Message (16 bits)
- Bits [15:12]: Message type (0x7 = spike)
- Bits [11:0]: Local neuron ID (0-4095)

#### Weight Update Message (64 bits, 4 frames)
- Frame 1: [0x71][target_node_id]
- Frame 2: [neuron_id[15:8]][neuron_id[7:0]]
- Frame 3: [synapse_idx[7:0]][weight[15:8]]
- Frame 4: [weight[7:0]][reserved]

## Data Structures

### Neuron Table Entry (256 bytes in PSRAM)

```c
typedef struct {
    // Neuron state (16 bytes)
    uint16_t neuron_id;           // Local neuron ID (0-4095)
    uint16_t flags;               // Status flags
    float membrane_potential;     // Current membrane potential
    float threshold;              // Spike threshold
    uint32_t last_spike_time_us;  // Timestamp of last spike
    
    // Synapse table metadata (8 bytes)
    uint16_t synapse_count;       // Number of incoming synapses
    uint16_t synapse_capacity;    // Max synapses allocated
    uint32_t synapse_table_offset; // Offset to synapse table
    
    // Reserved for future use (8 bytes)
    uint32_t reserved[2];
    
    // Synapse entries (60 × 4 bytes = 240 bytes)
    // Each synapse: [source_neuron_id:24][weight:8]
    uint32_t synapses[60];
} neuron_entry_t;
```

### Global Neuron Addressing

A 24-bit global neuron ID encodes both node location and local neuron index:
- Bits [23:16]: Node ID (0-255)
- Bits [15:0]: Local neuron ID (0-65535)

This allows up to 256 nodes × 65536 neurons = 16.7M neurons globally.

### SNN Topology JSON Format

```json
{
    "network_name": "example_snn",
    "neuron_count": 30000,
    "layers": [
        {
            "layer_id": 0,
            "layer_type": "input",
            "neuron_count": 784,
            "neuron_ids": [0, 783]
        },
        {
            "layer_id": 1,
            "layer_type": "hidden",
            "neuron_count": 1000,
            "neuron_ids": [784, 1783],
            "threshold": 1.0,
            "leak_rate": 0.95
        },
        {
            "layer_id": 2,
            "layer_type": "output",
            "neuron_count": 10,
            "neuron_ids": [1784, 1793],
            "threshold": 1.0
        }
    ],
    "connections": [
        {
            "source_layer": 0,
            "target_layer": 1,
            "connection_type": "fully_connected",
            "weight_init": "random_normal",
            "weight_mean": 0.5,
            "weight_stddev": 0.1
        },
        {
            "source_layer": 1,
            "target_layer": 2,
            "connection_type": "fully_connected",
            "weight_init": "random_normal",
            "weight_mean": 0.5,
            "weight_stddev": 0.1
        }
    ],
    "node_assignment": {
        "strategy": "balanced",
        "nodes": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    }
}
```

## Python Utility Design

### Command-Line Interface

Each utility follows standard Unix conventions with options, arguments, and exit codes.

#### nls - List Nodes

```
Usage: nls [OPTIONS]

List all compute nodes in the cluster.

Options:
  -c, --controller IP    Controller IP address (default: 192.168.1.222)
  -v, --verbose          Show detailed node information
  -j, --json             Output in JSON format

Examples:
  nls                    # List all nodes
  nls -v                 # Verbose output with memory info
  nls -j                 # JSON output for scripting
```

#### ncd - Connect to Node

```
Usage: ncd NODE [OPTIONS]

Connect to a node's virtual memory space.

Arguments:
  NODE                   Node ID (0-15)

Options:
  -c, --controller IP    Controller IP address
  -i, --interactive      Interactive memory browser

Examples:
  ncd 0                  # Show node 0 info
  ncd 0 -i               # Interactive memory browser
```

#### ncp - Copy to Node

```
Usage: ncp SOURCE DEST [OPTIONS]

Copy files to node memory.

Arguments:
  SOURCE                 Local file path
  DEST                   Node destination (node_id/path or node_id@addr)

Options:
  -c, --controller IP    Controller IP address
  -v, --verbose          Show progress

Examples:
  ncp weights.bin 0/weights           # Copy to node 0 named location
  ncp firmware.bin 0@0x20000000       # Copy to specific address
```

#### ncat - Display Node Data

```
Usage: ncat NODE/PATH [OPTIONS]

Display data from node memory.

Arguments:
  NODE/PATH              Node and memory path

Options:
  -c, --controller IP    Controller IP address
  -x, --hex              Hexadecimal output
  -b, --binary           Binary output
  -n, --neurons          Parse as neuron table

Examples:
  ncat 0/weights         # Display weights table
  ncat 0/weights -x      # Hex dump
  ncat 0@0x20000000 -n   # Parse neuron table at address
```

#### nstat - Cluster Status

```
Usage: nstat [OPTIONS]

Show cluster status and statistics.

Options:
  -c, --controller IP    Controller IP address
  -w, --watch SECONDS    Refresh every N seconds
  -s, --snn              Show SNN activity statistics

Examples:
  nstat                  # One-time status
  nstat -w 1             # Live monitoring
  nstat -s               # SNN statistics
```

#### nping - Ping Node

```
Usage: nping NODE [OPTIONS]

Ping a specific node to test connectivity.

Arguments:
  NODE                   Node ID (0-15) or 'all'

Options:
  -c, --controller IP    Controller IP address
  -n, --count N          Number of pings (default: 4)

Examples:
  nping 0                # Ping node 0
  nping all              # Ping all nodes
```

#### nreset - Reset Node

```
Usage: nreset NODE [OPTIONS]

Reset a specific node.

Arguments:
  NODE                   Node ID (0-15) or 'all'

Options:
  -c, --controller IP    Controller IP address
  -f, --force            Skip confirmation

Examples:
  nreset 0               # Reset node 0 (with confirmation)
  nreset all -f          # Reset all nodes without confirmation
```

#### ndiscover - Discover Nodes

```
Usage: ndiscover [OPTIONS]

Discover all active nodes in the cluster.

Options:
  -c, --controller IP    Controller IP address
  -v, --verbose          Show discovery progress

Examples:
  ndiscover              # Discover nodes
  ndiscover -v           # Verbose discovery
```

#### nload - Load Binary

```
Usage: nload NODE BINARY [OPTIONS]

Load binary code to node memory.

Arguments:
  NODE                   Node ID (0-15)
  BINARY                 Binary file path

Options:
  -c, --controller IP    Controller IP address
  -a, --addr ADDRESS     Load address (default: 0x20000000)
  -v, --verify           Verify after loading

Examples:
  nload 0 neuron_code.bin              # Load to default address
  nload 0 neuron_code.bin -a 0x20001000 # Load to specific address
  nload 0 neuron_code.bin -v           # Load and verify
```

#### nrun - Execute Code

```
Usage: nrun NODE [OPTIONS]

Execute code on a node.

Arguments:
  NODE                   Node ID (0-15)

Options:
  -c, --controller IP    Controller IP address
  -a, --addr ADDRESS     Entry point address (default: 0x20000000)
  -p, --params PARAMS    Parameters as comma-separated values

Examples:
  nrun 0                           # Execute at default address
  nrun 0 -a 0x20001000             # Execute at specific address
  nrun 0 -p 1,2,3                  # Execute with parameters
```

#### nsnn - SNN Management

```
Usage: nsnn COMMAND [OPTIONS]

Manage Spiking Neural Networks on the cluster.

Commands:
  deploy TOPOLOGY        Deploy SNN from topology file
  status                 Show SNN status
  start                  Start SNN execution
  stop                   Stop SNN execution
  monitor DURATION       Monitor spike activity
  inject SPIKES          Inject input spikes

Options:
  -c, --controller IP    Controller IP address

Examples:
  nsnn deploy network.json           # Deploy SNN topology
  nsnn status                        # Show current SNN status
  nsnn start                         # Start SNN execution
  nsnn monitor 5000                  # Monitor for 5 seconds
  nsnn inject input_spikes.json      # Inject input pattern
```

## Implementation Strategy

### Phase 1: Python Utilities Core

Implement base HTTP client library and common utilities (nls, nping, nstat, ndiscover). These establish the communication foundation and verify cluster connectivity.

### Phase 2: Memory Operations

Implement memory access utilities (ncd, ncp, ncat, nload, nrun). These enable low-level cluster programming and debugging.

### Phase 3: Embedded HTTP Server

Extend tcp_server_test.c with RESTful endpoints for node management and memory operations. Implement chunked transfer encoding for large data transfers.

### Phase 4: SNN Framework

Implement topology compiler, weight distribution, and spike routing. Add SNN-specific bus commands to z1_matrix_bus.c.

### Phase 5: SNN Utilities

Implement nsnn utility and supporting tools for SNN deployment and monitoring.

## Security Considerations

The current design assumes a trusted local network environment. For production deployment, the following security measures should be considered:

- API authentication via bearer tokens
- TLS encryption for HTTP traffic
- Rate limiting on bus operations
- Memory access validation and bounds checking
- Firmware signature verification

## Performance Optimization

### Bus Utilization

The current software-driven bus operates at ~100μs per 16-bit transfer. Migration to PIO-based implementation could achieve 20-80MHz transfer rates, reducing latency by 100-1000×.

### Memory Bandwidth

PSRAM access via QSPI provides ~20MB/s bandwidth. Neuron table reads are cached in node SRAM during execution to minimize PSRAM access.

### Spike Routing

Broadcast spike messages enable efficient one-to-many neuron communication. Nodes filter spikes based on local neuron connectivity tables, avoiding unnecessary processing.

### HTTP Server Optimization

Chunked transfer encoding enables streaming large memory dumps without buffering. JSON responses are generated incrementally to minimize memory usage on the controller node.
