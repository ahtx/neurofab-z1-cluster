# Z1 Cluster Emulator Architecture

## Overview

The Z1 Cluster Emulator is a software simulation of the NeuroFab Z1 neuromorphic computing cluster. It provides a complete virtual environment for developing, testing, and demonstrating SNN applications without requiring physical hardware.

## Design Goals

1. **Fidelity** - Accurately simulate hardware behavior including timing, memory constraints, and bus communication
2. **Performance** - Run fast enough for interactive development and testing
3. **Compatibility** - Work with existing tools with minimal or no modifications
4. **Observability** - Provide visibility into internal state for debugging and education
5. **Scalability** - Support multi-backplane configurations (200+ nodes)

## Architecture Layers

### Layer 1: Hardware Emulation

**Components:**
- **ComputeNode** - Simulates RP2350B with 8MB PSRAM
- **ControllerNode** - Simulates controller with Ethernet interface
- **Backplane** - Simulates 16-node backplane with matrix bus
- **Cluster** - Manages multiple backplanes

**Features:**
- Memory simulation (Flash: 2MB, PSRAM: 8MB per node)
- Bus communication with realistic latency
- LED state tracking
- Uptime and statistics

### Layer 2: Firmware Emulation

**Components:**
- **Bootloader** - Simulates firmware update mechanism
- **SNN Engine** - Executes LIF neuron model
- **Bus Protocol** - Implements Z1 bus commands

**Features:**
- Firmware loading and verification
- CRC32 checking
- Boot mode selection
- Neuron table processing
- Spike propagation

### Layer 3: Network Interface

**Components:**
- **HTTP Server** - Flask-based REST API server
- **API Endpoints** - Compatible with real hardware API

**Features:**
- All /api/* endpoints from real hardware
- JSON responses
- Binary data transfer (base64 encoded)
- Multi-backplane support

### Layer 4: Visualization & Debugging

**Components:**
- **Web Dashboard** - Real-time cluster visualization
- **Spike Monitor** - Live spike activity display
- **Memory Inspector** - View node memory contents
- **Network Visualizer** - SNN topology display

## Memory Model

### Flash Memory (2MB per node)
```
0x10000000 - 0x10003FFF  Bootloader (16KB) [read-only in emulator]
0x10004000 - 0x1001FFFF  Application Firmware (112KB)
0x10020000 - 0x1003FFFF  Firmware Update Buffer (128KB)
0x10040000 - 0x101FFFFF  User Data / Filesystem (1.75MB)
```

### PSRAM (8MB per node)
```
0x20000000 - 0x200FFFFF  Bootloader RAM (1MB)
0x20100000 - 0x207FFFFF  Application Data (7MB)
  - Neuron tables
  - Synapse weights
  - Spike buffers
  - Application heap
```

## Bus Simulation

The emulator simulates the Z1 matrix bus with:
- **Message queue** per node
- **Configurable latency** (default: 1-5μs)
- **Broadcast support** for spike messages
- **Command/response** pattern for memory operations

## SNN Execution

The emulator runs SNNs using:
- **Event-driven simulation** - Process spikes as they arrive
- **Timestep simulation** - Advance time in discrete steps
- **Real-time mode** - Run at wall-clock speed
- **Fast mode** - Run as fast as possible

### LIF Neuron Model

```python
# Membrane potential update
V_new = V_old * leak_rate + sum(incoming_spikes * weights)

# Spike generation
if V_new >= threshold:
    generate_spike()
    V_new = 0  # Reset
```

## API Compatibility

The emulator implements all endpoints from the real hardware:

### Node Management
- GET /api/nodes
- GET /api/nodes/{id}
- POST /api/nodes/{id}/reset
- GET /api/nodes/{id}/memory
- POST /api/nodes/{id}/memory
- POST /api/nodes/{id}/execute

### SNN Management
- POST /api/snn/deploy
- GET /api/snn/topology
- POST /api/snn/weights
- GET /api/snn/activity
- POST /api/snn/input
- POST /api/snn/start
- POST /api/snn/stop

### Firmware Management
- GET /api/nodes/{id}/firmware
- POST /api/nodes/{id}/firmware

### Emulator-Specific Endpoints
- GET /api/emulator/status
- POST /api/emulator/reset
- GET /api/emulator/config
- POST /api/emulator/config
- GET /api/emulator/dashboard

## Configuration

The emulator is configured via JSON:

```json
{
  "backplanes": [
    {
      "name": "backplane-0",
      "controller_ip": "127.0.0.1",
      "controller_port": 8000,
      "node_count": 16
    }
  ],
  "simulation": {
    "mode": "real-time",
    "timestep_us": 1000,
    "bus_latency_us": 2
  },
  "memory": {
    "flash_size_mb": 2,
    "psram_size_mb": 8
  },
  "visualization": {
    "enabled": true,
    "dashboard_port": 8080
  }
}
```

## Tool Compatibility

### Automatic Detection

Tools automatically detect emulator vs real hardware:

```python
# Check if running against emulator
response = requests.get(f"http://{controller_ip}/api/emulator/status")
if response.status_code == 200:
    # Emulator detected
    emulator_mode = True
```

### Emulator-Specific Features

When running against emulator, tools can:
- Access additional debugging endpoints
- View internal state
- Control simulation speed
- Capture detailed traces

## Performance Characteristics

### Real Hardware
- Bus latency: 1-5μs
- Memory access: ~100ns
- Spike propagation: ~10μs
- Network latency: ~1ms (Ethernet)

### Emulator
- Bus latency: Configurable (simulated)
- Memory access: Instant (Python dict)
- Spike propagation: ~100μs (Python overhead)
- Network latency: ~1ms (localhost HTTP)

## Use Cases

### 1. Development
- Develop SNN algorithms without hardware
- Test cluster management tools
- Prototype firmware

### 2. Testing
- Automated integration tests
- Regression testing
- Performance benchmarking

### 3. Education
- Learn neuromorphic computing concepts
- Visualize spike propagation
- Experiment with network topologies

### 4. Demonstration
- Show capabilities without hardware
- Interactive web dashboard
- Record and replay experiments

## Implementation Stack

- **Language**: Python 3.7+
- **Web Framework**: Flask
- **Visualization**: HTML5 + JavaScript (D3.js)
- **Testing**: pytest
- **Dependencies**: numpy, flask, requests

## Future Enhancements

- [ ] GPU acceleration for large networks
- [ ] Distributed emulation across multiple machines
- [ ] Hardware-in-the-loop testing
- [ ] Timing-accurate simulation mode
- [ ] Power consumption modeling
- [ ] Fault injection for robustness testing
