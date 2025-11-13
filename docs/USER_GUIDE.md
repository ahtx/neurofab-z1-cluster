# Z1 Neuromorphic Cluster - User Guide

**Version:** 3.0 (Production Ready)  
**Date:** November 13, 2025  
**Firmware:** Controller v3.0, Node v2.1

---

## Table of Contents

1. [Introduction](#introduction)
2. [Hardware Setup](#hardware-setup)
3. [Firmware Installation](#firmware-installation)
4. [Software Installation](#software-installation)
5. [First Connection](#first-connection)
6. [CLI Tools Reference](#cli-tools-reference)
7. [Working with SNNs](#working-with-snns)
8. [HTTP API Usage](#http-api-usage)
9. [Troubleshooting](#troubleshooting)
10. [Advanced Topics](#advanced-topics)

---

## Introduction

The NeuroFab Z1 is a production-ready neuromorphic computing cluster designed for distributed Spiking Neural Network (SNN) execution. This guide provides step-by-step instructions for hardware setup, firmware installation, and cluster management.

### What You'll Learn

- How to assemble and configure Z1 hardware
- How to flash production firmware to RP2350B boards
- How to use Python CLI tools for cluster management
- How to deploy and execute SNNs across the cluster
- How to monitor and debug system operation

### System Capabilities

- **Neurons per Node:** Up to 1024 (limited by PSRAM allocation)
- **Nodes per Cluster:** Up to 16 (single backplane)
- **Total Capacity:** 16,384 neurons across cluster
- **Simulation Rate:** 1ms timestep (1000 Hz)
- **Learning:** STDP (Spike-Timing-Dependent Plasticity)
- **Communication:** 16-bit parallel matrix bus + HTTP API

---

## Hardware Setup

### Required Components

#### Controller Node (1 per cluster)
- Raspberry Pi RP2350B development board
- W5500 Ethernet module (SPI interface)
- SSD1306 OLED display 128x64 (I2C, optional but recommended)
- APS6404L 8MB QSPI PSRAM chip
- 5V power supply (USB or external)
- Ethernet cable

#### Compute Node (1-16 per cluster)
- Raspberry Pi RP2350B development board
- APS6404L 8MB QSPI PSRAM chip
- RGB LED (common cathode, optional)
- 5V power supply (USB or external)
- Node ID resistors (for GPIO40-43 strapping)

#### Interconnect
- 16-bit parallel bus (ribbon cable or PCB traces)
- Common ground connection between all nodes

### Wiring Diagram

#### Controller Node Connections

| Component | RP2350B Pin | Notes |
|-----------|-------------|-------|
| **W5500 Ethernet** | | |
| MISO | GPIO16 | SPI RX |
| MOSI | GPIO19 | SPI TX |
| SCK | GPIO18 | SPI Clock |
| CS | GPIO17 | Chip Select |
| RST | GPIO20 | Reset (optional) |
| **SSD1306 OLED** | | |
| SDA | GPIO4 | I2C Data |
| SCL | GPIO5 | I2C Clock |
| **PSRAM (APS6404L)** | | |
| CS | QSPI_SS | Chip Select |
| SCK | QSPI_SCLK | Clock |
| D0-D3 | QSPI_SD0-3 | Data lines |
| **Matrix Bus** | | |
| DATA[0:15] | GPIO0-15 | 16-bit parallel |
| **Status LED** | | |
| LED | GPIO25 | Built-in LED |

#### Compute Node Connections

| Component | RP2350B Pin | Notes |
|-----------|-------------|-------|
| **PSRAM (APS6404L)** | | |
| CS | QSPI_SS | Chip Select |
| SCK | QSPI_SCLK | Clock |
| D0-D3 | QSPI_SD0-3 | Data lines |
| **Matrix Bus** | | |
| DATA[0:15] | GPIO0-15 | 16-bit parallel |
| **Node ID Strapping** | | |
| ID_BIT0 | GPIO40 | LSB (pull high/low) |
| ID_BIT1 | GPIO41 | |
| ID_BIT2 | GPIO42 | |
| ID_BIT3 | GPIO43 | MSB (pull high/low) |
| **RGB LED (optional)** | | |
| RED | GPIO46 | PWM output |
| GREEN | GPIO44 | PWM output |
| BLUE | GPIO45 | PWM output |

### Node ID Configuration

Each compute node must have a unique ID (0-15). Configure via GPIO40-43:

| Node ID | GPIO43 | GPIO42 | GPIO41 | GPIO40 |
|---------|--------|--------|--------|--------|
| 0 | LOW | LOW | LOW | LOW |
| 1 | LOW | LOW | LOW | HIGH |
| 2 | LOW | LOW | HIGH | LOW |
| 3 | LOW | LOW | HIGH | HIGH |
| ... | ... | ... | ... | ... |
| 15 | HIGH | HIGH | HIGH | HIGH |

**Implementation:** Use pull-down resistors (10kΩ to GND) for LOW, leave floating or pull-up (10kΩ to 3.3V) for HIGH.

---

## Firmware Installation

### Download Production Firmware

```bash
git clone https://github.com/ahtx/neurofab-z1-cluster.git
cd neurofab-z1-cluster/firmware_releases
```

**Available Binaries:**
- `z1_controller_v3.0_PRODUCTION_READY.uf2` (120KB)
- `z1_node_v2.1_PRODUCTION_READY.uf2` (89KB)

### Flash Controller Node

1. **Enter Bootloader Mode:**
   - Disconnect USB cable
   - Hold BOOTSEL button on RP2350B
   - Connect USB cable while holding button
   - Release button after 2 seconds

2. **Copy Firmware:**
   - RP2350B will appear as USB mass storage device (RPI-RP2)
   - Copy `z1_controller_v3.0_PRODUCTION_READY.uf2` to the drive
   - Board will automatically reboot

3. **Verify Installation:**
   - OLED display should show "Z1 Controller Ready"
   - Built-in LED should blink slowly
   - Serial output (115200 baud) should show boot messages

### Flash Compute Nodes

1. **Configure Node ID:**
   - Set GPIO40-43 according to desired node ID (see table above)
   - Double-check connections before powering on

2. **Enter Bootloader Mode:**
   - Hold BOOTSEL button
   - Connect USB cable
   - Release button

3. **Copy Firmware:**
   - Copy `z1_node_v2.1_PRODUCTION_READY.uf2` to RPI-RP2 drive
   - Board will reboot automatically

4. **Verify Installation:**
   - RGB LED should show startup sequence: RED → GREEN → BLUE
   - Serial output should show node ID and "Ready for bus operations"

5. **Repeat for All Nodes:**
   - Flash each node with unique ID (0-15)
   - Verify each node boots successfully before proceeding

### Network Configuration

Controller defaults:
- **IP Address:** 192.168.1.222
- **Subnet Mask:** 255.255.255.0
- **Gateway:** 192.168.1.1
- **HTTP Port:** 80

To change IP address, edit `w5500_http_server.c` lines 150-153 and rebuild firmware.

---

## Software Installation

### Prerequisites

- Python 3.7 or higher
- pip package manager
- Network connectivity to controller

### Install Python Dependencies

```bash
pip3 install requests numpy
```

### Configure Environment

```bash
# Add utilities to PATH
export PATH=$PATH:/path/to/neurofab-z1-cluster/python_tools/bin

# Set controller IP (optional, defaults to 192.168.1.222)
export Z1_CONTROLLER_IP=192.168.1.222
```

To make permanent, add to `~/.bashrc`:

```bash
echo 'export PATH=$PATH:/path/to/neurofab-z1-cluster/python_tools/bin' >> ~/.bashrc
echo 'export Z1_CONTROLLER_IP=192.168.1.222' >> ~/.bashrc
source ~/.bashrc
```

---

## First Connection

### Test Network Connectivity

```bash
# Ping controller
ping 192.168.1.222

# Test HTTP API
curl http://192.168.1.222/api/status
```

Expected response:
```json
{
  "status": "online",
  "uptime_ms": 12345,
  "nodes_discovered": 0
}
```

### Discover Nodes

```bash
python_tools/bin/nls
```

Expected output:
```
Discovering nodes...
Found 4 active nodes:
  Node 0: READY
  Node 1: READY
  Node 2: READY
  Node 3: READY
```

### Test Node Communication

```bash
# Ping node 0
python_tools/bin/nping 0
```

Expected output:
```
Pinging node 0...
Response received: 0x42
Round-trip time: 12ms
```

---

## CLI Tools Reference

### nls - List Nodes

**Description:** Discover and list all active nodes in the cluster.

**Usage:**
```bash
python_tools/bin/nls [options]
```

**Options:**
- `-c, --controller IP` - Controller IP address (default: 192.168.1.222)
- `-p, --port PORT` - Controller port (default: 80)

**Example:**
```bash
$ python_tools/bin/nls
Discovering nodes...
Found 4 active nodes:
  Node 0: READY
  Node 1: READY
  Node 2: READY
  Node 3: READY
```

### nping - Ping Node

**Description:** Test connectivity to a specific node.

**Usage:**
```bash
python_tools/bin/nping <node_id> [options]
```

**Arguments:**
- `node_id` - Node ID (0-15)

**Options:**
- `-c, --controller IP` - Controller IP address
- `-t, --timeout MS` - Timeout in milliseconds (default: 1000)

**Example:**
```bash
$ python_tools/bin/nping 0
Pinging node 0...
Response received: 0x42
Round-trip time: 12ms
```

### nsnn - SNN Management

**Description:** Comprehensive SNN management tool for deployment, execution control, and monitoring.

**Usage:**
```bash
python_tools/bin/nsnn <command> [options]
```

**Commands:**
- `deploy <file>` - Deploy SNN from JSON file
- `start` - Start SNN execution
- `stop` - Stop SNN execution
- `status` - Show SNN status
- `monitor` - Monitor spike activity

**Arguments:**
- `network_file` - Path to network definition JSON

**Options:**
- `-c, --controller IP` - Controller IP address
- `-v, --verbose` - Verbose output

**Example:**
```bash
$ python_tools/bin/nsnn deploy examples/xor_network.json
Parsing network definition...
  Total neurons: 256
  Total synapses: 1024
  Nodes required: 1

Deploying to cluster...
  Node 0: Writing 256 neurons (65536 bytes)...
  Node 0: Sending load command...
  Node 0: Deployment complete

Network deployed successfully!
```

**Start SNN:**
```bash
$ python_tools/bin/nsnn start
Starting SNN execution...
  Node 0: Started
  Node 1: Started
  Node 2: Started
  Node 3: Started

SNN execution started on 4 nodes.
```

**Stop SNN:**
```bash
$ python_tools/bin/nsnn stop
Stopping SNN execution...
  Node 0: Stopped
  Node 1: Stopped
  Node 2: Stopped
  Node 3: Stopped

SNN execution stopped on 4 nodes.
```

### nstat - Cluster Status

**Description:** Display cluster status and statistics.

**Usage:**
```bash
python_tools/bin/nstat [options]
```

**Options:**
- `-c, --controller IP` - Controller IP address

**Example:**
```bash
$ python_tools/bin/nstat
Z1 Cluster Status:
  Controller: 192.168.1.222
  Active Nodes: 4/16
  SNN Status: Running
  Total Spikes: 15847
```

### ncat - Display Node Memory

**Description:** Read and display memory contents from a node.

**Usage:**
```bash
python_tools/bin/ncat <node_id> <address> <length> [options]
```

**Example:**
```bash
$ python_tools/bin/ncat 0 0x20000000 256
Reading 256 bytes from node 0 at 0x20000000...
[hex dump output]
```

### ncp - Copy File to Node

**Description:** Copy a file to node memory.

**Usage:**
```bash
python_tools/bin/ncp <file> <node_id> [address] [options]
```

**Example:**
```bash
$ python_tools/bin/ncp data.bin 0
Copying data.bin to node 0...
Wrote 1024 bytes
```

### nflash - Flash Firmware

**Description:** Flash firmware to compute nodes.

**Usage:**
```bash
python_tools/bin/nflash <firmware_file> <node_id> [options]
```

**Example:**
```bash
$ python_tools/bin/nflash z1_node.uf2 0
Flashing firmware to node 0...
Upload complete. Node will reboot.
```

### nconfig - Manage Configuration

**Description:** Manage multi-backplane cluster configuration.

**Usage:**
```bash
python_tools/bin/nconfig <command> [options]
```

**Commands:**
- `list` - List backplanes
- `add` - Add backplane
- `remove` - Remove backplane

**Example:**
```bash
$ python_tools/bin/nconfig list
Configured backplanes:
  Backplane 0: 192.168.1.222 (4 nodes)
```

---

## Working with SNNs

### Network Definition Format

Networks are defined in JSON format:

```json
{
  "name": "XOR Network",
  "neurons": [
    {
      "id": 0,
      "node": 0,
      "threshold": 1.0,
      "leak_rate": 0.1,
      "refractory_period_us": 5000,
      "synapses": [
        {"source": 2, "weight": 0.8},
        {"source": 3, "weight": 0.8}
      ]
    },
    ...
  ]
}
```

### Deploying a Network

1. **Create Network Definition:**
   - Define neurons with parameters
   - Specify synaptic connections
   - Assign neurons to nodes

2. **Deploy to Cluster:**
   ```bash
   python_tools/bin/ndeploy my_network.json
   ```

3. **Start Execution:**
   ```bash
   python_tools/bin/nstart
   ```

4. **Inject Test Inputs:**
   ```bash
   python_tools/bin/ninject 0 0 1.0
   python_tools/bin/ninject 0 1 1.0
   ```

5. **Monitor Activity:**
   - Check OLED display for spike statistics
   - View serial output for detailed logs

6. **Stop Execution:**
   ```bash
   python_tools/bin/nstop
   ```

---

## HTTP API Usage

### Using curl

```bash
# Get system status
curl http://192.168.1.222/api/status

# Discover nodes
curl -X POST http://192.168.1.222/api/nodes/discover

# Ping node 0
curl -X POST http://192.168.1.222/api/nodes/0/ping

# Start SNN execution
curl -X POST http://192.168.1.222/api/snn/start

# Stop SNN execution
curl -X POST http://192.168.1.222/api/snn/stop
```

### Using Python requests

```python
import requests

BASE_URL = "http://192.168.1.222"

# Get status
response = requests.get(f"{BASE_URL}/api/status")
print(response.json())

# Discover nodes
response = requests.post(f"{BASE_URL}/api/nodes/discover")
nodes = response.json()["active_nodes"]
print(f"Found {len(nodes)} nodes")

# Start SNN
response = requests.post(f"{BASE_URL}/api/snn/start")
print(response.json())
```

Full API documentation: [API_REFERENCE.md](API_REFERENCE.md)

---

## Troubleshooting

### Controller Not Responding

**Symptoms:** Cannot ping controller, HTTP requests timeout

**Solutions:**
1. Check power supply (5V, ≥1A)
2. Verify Ethernet cable connection
3. Check network configuration (IP, subnet, gateway)
4. Verify OLED shows "Z1 Controller Ready"
5. Connect USB and check serial output (115200 baud)
6. Try power cycle (disconnect power for 10 seconds)

### Node Not Discovered

**Symptoms:** Node missing from `nls` output

**Solutions:**
1. Verify matrix bus connections (GPIO0-15)
2. Check node ID strapping (GPIO40-43)
3. Verify power supply to node
4. Check LED startup sequence (should show R→G→B)
5. Connect USB and check serial output
6. Verify node firmware is flashed correctly
7. Try manual ping: `python_tools/bin/nping <node_id>`

### SNN Deployment Fails

**Symptoms:** Deployment command returns error

**Solutions:**
1. Verify all nodes are discovered
2. Check neuron count ≤ 1024 per node
3. Validate JSON format
4. Check PSRAM initialization in serial logs
5. Verify sufficient memory available
6. Try deploying to single node first

### Spikes Not Propagating

**Symptoms:** No inter-node spike activity

**Solutions:**
1. Verify SNN is started: `python_tools/bin/nstart`
2. Check synaptic connections in network definition
3. Verify source and target neurons on different nodes
4. Monitor serial output for spike routing messages
5. Check matrix bus signal integrity
6. Verify multi-frame protocol working

### OLED Display Issues

**Symptoms:** Display blank or garbled

**Solutions:**
1. Check I2C connections (SDA=GPIO4, SCL=GPIO5)
2. Verify I2C address (0x3C default)
3. Check power supply to display (3.3V)
4. Try different I2C speed (edit firmware)
5. Display is optional - system works without it

---

## Advanced Topics

### Custom Network Topologies

Create complex networks with:
- Multiple layers (input, hidden, output)
- Recurrent connections
- Lateral inhibition
- Configurable time constants

### STDP Learning

Enable online learning:
- Configure STDP parameters in neuron definition
- Weight updates occur during execution
- No redeployment needed

### Performance Optimization

- Minimize inter-node connections (reduce bus traffic)
- Use neuron cache effectively (locality of reference)
- Adjust simulation timestep for speed/accuracy tradeoff
- Monitor cache hit rate in serial output

### Firmware Customization

Build from source to customize:
- Neuron model parameters
- Cache size and policy
- Network configuration (IP address, port)
- Display messages and formatting

See [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) for build instructions.

---

## Next Steps

- Read [SNN_GUIDE.md](SNN_GUIDE.md) for detailed neuron model documentation
- Review [API_REFERENCE.md](API_REFERENCE.md) for complete API specification
- Study [ARCHITECTURE.md](ARCHITECTURE.md) to understand system design
- Check [CODE_WALKTHROUGH.md](CODE_WALKTHROUGH.md) for firmware internals

---

**Version:** 3.0  
**Last Updated:** November 13, 2025  
**Status:** Production Ready
