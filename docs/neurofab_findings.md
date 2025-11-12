# NeuroFab Z1 Architecture Analysis

## System Overview

### Hardware Architecture
- **Z1 v1**: Neuromorphic computing platform for Spiking Neural Networks (SNNs)
- **Backplane Design**: 16 compute nodes + 1 controller node per backplane
- **Scalability**: 6 backplanes networked via Ethernet (up to 10 2U units per rack)
- **Total Capacity**: 102 RP2350 processors × 2 cores = 204-408 cores per rack
- **Memory per Node**: 8MB PSRAM (APS6404L-3SQR-SN) + 16MB Flash (W25Q128JVPIQ)
- **Total Memory**: ~2.5GB aggregate across 102 nodes
- **Power**: ~1.8W per processor, 180W per 2U unit, 1.8kW per rack

### Processor Specifications
- **MCU**: RP2350B (Raspberry Pi Pico 2)
- **Cores**: Dual-core ARM Cortex-M33 and/or Hazard3 RISC-V
- **Clock**: Up to 200 MHz per core
- **Aggregate Performance**: ~25 billion instructions/second
- **PIO**: Programmable I/O state machines for bus offloading

### Communication Architecture

#### Z1 Matrix Bus (Intra-Backplane)
- **Topology**: Multi-master 26-line shared GPIO bus
- **Data Bus**: 16-bit bidirectional (GPIO12-27)
- **Address Bus**: 5-bit (GPIO7-11) - supports 32 addresses
- **Control Signals**: 
  - BUSATTN (GPIO2) - Bus attention (active low to claim)
  - BUSACK (GPIO3) - Bus acknowledge (target confirms ready)
  - BUSCLK (GPIO6) - Bus clock for synchronized transfers
- **Node Addressing**:
  - 0x00-0x0F (0-15): Compute nodes
  - 0x10 (16): Controller node
  - 0x1F (31): Broadcast address
- **Protocol**: Software-driven, multi-master with collision avoidance
- **Speed**: 20-80MHz @ 16-bits (theoretical with PIO optimization)

#### Bus Protocol Flow
1. Originator checks BUSATTN line (high = available)
2. Originator pulls BUSATTN low, sets address and data
3. First frame: 0xAA (header) + originator ID
4. Target pulls BUSACK low to acknowledge
5. Originator pulses BUSCLK to transfer data frames
6. Transaction ends when BUSATTN released (goes high)
7. Broadcast: Single frame, no handshake

#### Current Bus Commands
- 0x10: Set green LED (0-255 PWM)
- 0x20: Set red LED (0-255 PWM)
- 0x30: Set blue LED (0-255 PWM)
- 0x40: Status request
- 0x99: Ping command with 8-bit data

#### Inter-Backplane Communication
- **Network**: Standard Ethernet (100Mbps)
- **Controller**: W5500 Ethernet controller on controller node
- **Protocol**: TCP/IP
- **Expansion**: Simple rack-level scaling via Ethernet switch

### Controller Node Peripherals
- **W5500 Ethernet**: 100Mbps network connectivity (SPI0)
- **SD Card**: Removable storage for firmware/data (SPI1)
- **OLED Display**: SSD1306 I2C status display
- **LED Indicators**: RGB LEDs (GPIO44-46) on all nodes

### Node ID Configuration
- **Hardware**: GPIO40-43 used for node ID (0-15)
- **Controller**: Fixed ID 0x10 (16)
- **Auto-detection**: Nodes read hardware pins at boot

## Software Architecture

### Current Implementation
- **Language**: C (Pico SDK)
- **RTOS**: None (bare metal with interrupts)
- **Bus Library**: z1_matrix_bus.h/c
- **Web Server**: Basic HTTP server on controller (tcp_server_test.c)
- **File System**: FatFS for SD card access

### Bus Timing Parameters (Tunable)
- Clock high: 100μs
- Clock low: 50μs
- ACK timeout: 10ms
- Backoff base: 50μs
- Broadcast hold: 10ms

## SNN Architecture Requirements

### Neuron Distribution
- **Target**: 3 million neurons × 400 connections = 1 billion synapses
- **Per Node**: ~30,000 neurons (300k total / 102 nodes)
- **Memory per Neuron**: ~256 bytes (8MB / 30k neurons)
  - Neuron state: 16 bytes
  - Synapse table: ~240 bytes (60 synapses @ 4 bytes each)

### Message-Based Coordination
- **Spike Messages**: Source neuron ID + timestamp
- **Weight Update Messages**: Target neuron + synapse index + new weight
- **Addressing Scheme**: 
  - Node ID (8 bits) + Local Neuron ID (16 bits) = 24-bit global address
  - Supports up to 16M neurons globally

### Execution Model
1. Node boots, loads neuron table from PSRAM
2. Main loop:
   - Poll bus for incoming spike messages
   - Process spikes, update membrane potentials
   - Generate outbound spikes when threshold crossed
   - Send spike messages to connected neurons
3. Weight updates via separate command channel

## Key Files Analyzed
- NeuroFabTechIntro-11-11-25.pdf
- Z1ComputerHardwareArchitecture.docx
- z1_matrix_bus.h/c
- node.c
- tcp_server_test.c
- psram_rp2350.h/c

## Design Decisions for Utilities

### Python Cluster Management Tools (Unix-style)
- `nls` - List all compute nodes
- `ncd <node>` - Connect to node's virtual memory
- `ncp <file> <node>/path` - Copy files to node memory
- `ncat <node>/path` - Display node memory/tables
- `nstat` - Show cluster status
- `nping <node>` - Ping specific node
- `nreset <node>` - Reset specific node
- `ndiscover` - Discover all active nodes
- `nload <node> <binary>` - Load code to node
- `nrun <node>` - Execute code on node

### HTTP REST API Design (Embedded Server)
- GET /api/nodes - List all nodes
- GET /api/nodes/{id} - Get node info
- GET /api/nodes/{id}/memory - Read memory range
- POST /api/nodes/{id}/memory - Write memory
- POST /api/nodes/{id}/reset - Reset node
- POST /api/nodes/{id}/execute - Execute code
- GET /api/snn/topology - Get SNN network topology
- POST /api/snn/deploy - Deploy SNN to cluster
- POST /api/snn/weights - Update weights
- GET /api/snn/activity - Get spike activity

### SNN Framework Components
1. **Topology Compiler**: Convert SNN definition to node assignments
2. **Weight Loader**: Distribute synapse tables to nodes
3. **Spike Router**: Message routing based on connectivity
4. **Activity Monitor**: Collect and visualize spike trains
5. **Training Interface**: Online weight updates via STDP
