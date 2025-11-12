# NeuroFab Z1 Cluster User Guide

**Author:** NeuroFab  
**Date:** November 11, 2025

## Table of Contents

1. [Introduction](#introduction)
2. [System Overview](#system-overview)
3. [Installation](#installation)
4. [Command Reference](#command-reference)
5. [Working with SNNs](#working-with-snns)
6. [Advanced Topics](#advanced-topics)
7. [Troubleshooting](#troubleshooting)

## Introduction

The NeuroFab Z1 is a neuromorphic computing cluster designed for massively parallel, event-driven computation using Spiking Neural Networks (SNNs). This guide provides comprehensive instructions for managing the cluster, deploying neural networks, and monitoring system performance.

The Z1 cluster consists of up to 16 compute nodes per backplane, each powered by a Raspberry Pi RP2350B microcontroller with 8MB of PSRAM. The nodes communicate via a custom high-speed matrix bus, allowing for efficient spike propagation and coordinated computation.

## System Overview

### Hardware Architecture

The Z1 cluster is organized into a hierarchical structure:

- **Backplane**: A single PCB containing up to 16 compute nodes and 1 controller node
- **Compute Nodes**: RP2350B-based processors with 8MB PSRAM for neuron storage
- **Controller Node**: RP2350B with W5500 Ethernet controller for external communication
- **Matrix Bus**: 16-bit parallel GPIO bus connecting all nodes on a backplane

Each compute node can host up to 4,096 neurons with up to 60 synapses per neuron. This allows a single backplane to support networks with tens of thousands of neurons and millions of synapses.

### Software Architecture

The software stack consists of three layers:

**Layer 1: Python Cluster Management Tools**

A suite of Unix-style command-line utilities that run on a user's workstation. These tools communicate with the controller node over Ethernet using HTTP/REST API calls.

**Layer 2: Embedded HTTP Server**

A lightweight web server running on the controller node. It exposes a RESTful API and translates HTTP requests into Z1 bus commands.

**Layer 3: SNN Execution Engine**

The core computational engine running on each compute node. It implements Leaky Integrate-and-Fire (LIF) neurons, processes incoming spikes from the bus, and generates outbound spikes.

### Communication Flow

```
User Workstation → HTTP/REST → Controller Node → Z1 Bus → Compute Nodes
```

All cluster operations follow this pattern: the user issues a command via a Python utility, which sends an HTTP request to the controller. The controller translates this into one or more bus commands, which are executed by the target compute nodes.

## Installation

### Prerequisites

- Python 3.7 or higher
- Network connectivity to the Z1 controller node
- The `requests` library for Python

### Installing Python Tools

First, install the required Python dependencies:

```bash
pip3 install requests numpy
```

Next, add the Z1 tools to your system PATH. You can either create symbolic links:

```bash
cd /path/to/neurofab_system/python_tools/bin
sudo ln -s $(pwd)/* /usr/local/bin/
```

Or add the `bin` directory to your PATH environment variable:

```bash
export PATH=$PATH:/path/to/neurofab_system/python_tools/bin
```

To make this permanent, add the export command to your `~/.bashrc` or `~/.zshrc` file.

### Configuring Network Access

By default, the tools assume the controller node is at IP address `192.168.1.222`. If your controller is at a different address, you can specify it using the `-c` flag with each command:

```bash
nls -c 192.168.1.100
```

Alternatively, you can set the `Z1_CONTROLLER_IP` environment variable:

```bash
export Z1_CONTROLLER_IP=192.168.1.100
```

### Verifying Installation

Test your installation by listing the cluster nodes:

```bash
nls
```

If the installation is successful, you should see a list of active nodes with their status and memory information.

## Command Reference

### nls - List Nodes

The `nls` command lists all compute nodes in the cluster.

**Usage:**

```bash
nls [OPTIONS]
```

**Options:**

- `-c, --controller IP`: Controller IP address (default: 192.168.1.222)
- `-v, --verbose`: Show detailed node information
- `-j, --json`: Output in JSON format for scripting

**Examples:**

```bash
# List all nodes
nls

# Verbose output with memory info
nls -v

# JSON output for scripting
nls -j

# Use custom controller IP
nls -c 192.168.1.100
```

**Output:**

```
NODE  STATUS    MEMORY      UPTIME
--------------------------------------------------
   0  active    7.85 MB     2h 15m
   1  active    7.85 MB     2h 15m
   2  active    7.85 MB     2h 15m
   3  active    7.85 MB     2h 15m

Total: 4 nodes
```

### nping - Ping Nodes

The `nping` command tests connectivity to cluster nodes.

**Usage:**

```bash
nping NODE [OPTIONS]
```

**Arguments:**

- `NODE`: Node ID (0-15) or "all"

**Options:**

- `-c, --controller IP`: Controller IP address
- `-n, --count N`: Number of pings (default: 4)
- `-v, --verbose`: Verbose output

**Examples:**

```bash
# Ping node 0
nping 0

# Ping all nodes
nping all

# Ping node 0 ten times
nping 0 -n 10

# Ping all nodes with verbose output
nping all -v
```

**Output:**

```
4 packets transmitted, 4 received, 0.0% packet loss
rtt min/avg/max = 2.45/3.12/4.01 ms
```

### nstat - Cluster Status

The `nstat` command displays cluster status and statistics.

**Usage:**

```bash
nstat [OPTIONS]
```

**Options:**

- `-c, --controller IP`: Controller IP address
- `-w, --watch SECONDS`: Refresh every N seconds (live monitoring)
- `-s, --snn`: Show SNN activity statistics

**Examples:**

```bash
# Show cluster status once
nstat

# Live monitoring (refresh every 1 second)
nstat -w 1

# Show SNN activity statistics
nstat -s

# Live monitoring with SNN stats
nstat -w 2 -s
```

**Output:**

```
Z1 Cluster Status - 2025-11-11 23:30:45
================================================================================

Cluster Overview:
  Total Nodes:     12
  Active Nodes:    12
  Inactive Nodes:  0
  Total Memory:    94.20 MB

Node Status:
  NODE  STATUS    MEMORY      UPTIME      LED (R/G/B)
  ----------------------------------------------------------------------
     0  active    7.85 MB     2h 15m      0  /255/0  
     1  active    7.85 MB     2h 15m      0  /255/0  
     ...
```

### ncp - Copy to Nodes

The `ncp` command copies files to node memory.

**Usage:**

```bash
ncp SOURCE DEST [OPTIONS]
```

**Arguments:**

- `SOURCE`: Local file path
- `DEST`: Destination (node_id/location or node_id@address)

**Options:**

- `-c, --controller IP`: Controller IP address
- `-v, --verbose`: Show progress

**Named Memory Locations:**

- `weights`: 0x20000000 (start of PSRAM)
- `neurons`: 0x20100000 (1MB offset)
- `code`: 0x20200000 (2MB offset)
- `data`: 0x20300000 (3MB offset)
- `scratch`: 0x20700000 (7MB offset)

**Examples:**

```bash
# Copy to node 0 weights location
ncp weights.bin 0/weights

# Copy to specific address
ncp firmware.bin 0@0x20000000

# Copy with progress
ncp data.bin 0/data -v
```

**Output:**

```
weights.bin -> node 0:0x20000000 (128.00 KB)
```

### ncat - Display Node Data

The `ncat` command reads and displays node memory contents.

**Usage:**

```bash
ncat SOURCE [OPTIONS]
```

**Arguments:**

- `SOURCE`: Source (node_id/location or node_id@address)

**Options:**

- `-c, --controller IP`: Controller IP address
- `-x, --hex`: Hexadecimal dump
- `-b, --binary`: Binary output to stdout
- `-n, --neurons`: Parse as neuron table
- `-l, --length N`: Number of bytes to read

**Examples:**

```bash
# Display weights table
ncat 0/weights

# Hex dump
ncat 0/weights -x

# Display from specific address
ncat 0@0x20000000

# Parse as neuron table
ncat 0/neurons -n

# Display 512 bytes
ncat 0/data -l 512
```

**Output (neuron table):**

```
Neuron Table at 0x20100000 (10 entries)
================================================================================

Neuron 0 (Entry 0):
  Flags:              0x0001
  Membrane Potential: 0.0000
  Threshold:          1.0000
  Last Spike:         0 μs
  Synapses:           784/60
  Synapse Table:      0x20000000
  First Synapses:
    [0] Source: 0, Weight: 128
    [1] Source: 1, Weight: 132
    ...
```

### nsnn - SNN Management

The `nsnn` command manages Spiking Neural Networks on the cluster.

**Usage:**

```bash
nsnn COMMAND [ARGS] [OPTIONS]
```

**Commands:**

- `deploy TOPOLOGY`: Deploy SNN from topology JSON file
- `status`: Show SNN status and statistics
- `start`: Start SNN execution
- `stop`: Stop SNN execution
- `monitor DURATION`: Monitor spike activity (milliseconds)
- `inject SPIKES`: Inject input spikes from JSON file

**Options:**

- `-c, --controller IP`: Controller IP address

**Examples:**

```bash
# Deploy SNN topology
nsnn deploy network.json

# Show current SNN status
nsnn status

# Start SNN execution
nsnn start

# Monitor for 5 seconds
nsnn monitor 5000

# Inject input spikes
nsnn inject input_pattern.json
```

**Output (status):**

```
SNN Status:
============================================================
  State:           running
  Network:         MNIST_SNN_Classifier
  Neurons:         1794
  Active Neurons:  342
  Total Spikes:    15847
  Spike Rate:      3169.40 Hz
  Nodes Used:      12
```

## Working with SNNs

### SNN Topology Format

SNN topologies are defined in JSON format. The topology specifies the network structure, neuron parameters, and connectivity patterns.

**Basic Structure:**

```json
{
  "network_name": "my_network",
  "description": "Description of the network",
  "neuron_count": 1000,
  "layers": [
    {
      "layer_id": 0,
      "layer_type": "input",
      "neuron_count": 100,
      "neuron_ids": [0, 99],
      "threshold": 1.0,
      "leak_rate": 0.0
    },
    {
      "layer_id": 1,
      "layer_type": "hidden",
      "neuron_count": 800,
      "neuron_ids": [100, 899],
      "threshold": 1.0,
      "leak_rate": 0.95,
      "refractory_period_us": 1000
    },
    {
      "layer_id": 2,
      "layer_type": "output",
      "neuron_count": 100,
      "neuron_ids": [900, 999],
      "threshold": 1.0,
      "leak_rate": 0.95,
      "refractory_period_us": 2000
    }
  ],
  "connections": [
    {
      "source_layer": 0,
      "target_layer": 1,
      "connection_type": "fully_connected",
      "weight_init": "random_normal",
      "weight_mean": 0.5,
      "weight_stddev": 0.1,
      "delay_us": 1000
    },
    {
      "source_layer": 1,
      "target_layer": 2,
      "connection_type": "fully_connected",
      "weight_init": "random_normal",
      "weight_mean": 0.5,
      "weight_stddev": 0.1,
      "delay_us": 1000
    }
  ],
  "node_assignment": {
    "strategy": "balanced",
    "nodes": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    "neurons_per_node": 150
  },
  "simulation_params": {
    "timestep_us": 1000,
    "simulation_duration_ms": 100,
    "input_encoding": "rate_coding",
    "input_duration_ms": 50
  }
}
```

### Layer Types

**Input Layer**

Input layers receive external stimuli. They do not perform LIF integration and simply forward input values as spikes.

- `layer_type`: "input"
- `threshold`: Typically 1.0
- `leak_rate`: 0.0 (no leakage)

**Hidden Layer**

Hidden layers perform computation using LIF neurons. They integrate incoming spikes and generate output spikes when the membrane potential exceeds the threshold.

- `layer_type`: "hidden"
- `threshold`: Spike threshold (typically 1.0)
- `leak_rate`: Membrane leak rate (0.0-1.0, typically 0.95)
- `refractory_period_us`: Refractory period in microseconds

**Output Layer**

Output layers are similar to hidden layers but are designated as the network's output. Their spike patterns represent the network's decision or prediction.

- `layer_type`: "output"
- Parameters same as hidden layers

### Connection Types

**Fully Connected**

Every neuron in the source layer connects to every neuron in the target layer.

```json
{
  "connection_type": "fully_connected",
  "weight_init": "random_normal",
  "weight_mean": 0.5,
  "weight_stddev": 0.1
}
```

**Sparse Random**

Random sparse connectivity with a specified connection probability.

```json
{
  "connection_type": "sparse_random",
  "connection_probability": 0.1,
  "weight_init": "random_normal",
  "weight_mean": 0.5,
  "weight_stddev": 0.1
}
```

### Weight Initialization

**Random Normal**

Weights are drawn from a normal (Gaussian) distribution.

```json
{
  "weight_init": "random_normal",
  "weight_mean": 0.5,
  "weight_stddev": 0.1
}
```

**Random Uniform**

Weights are drawn from a uniform distribution.

```json
{
  "weight_init": "random_uniform",
  "weight_min": 0.0,
  "weight_max": 1.0
}
```

**Constant**

All weights are set to the same value.

```json
{
  "weight_init": "constant",
  "weight_value": 0.5
}
```

### Node Assignment Strategies

**Balanced**

Neurons are evenly distributed across all specified nodes.

```json
{
  "strategy": "balanced",
  "nodes": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
}
```

**Layer-Based**

Entire layers are assigned to specific nodes. This can improve performance for layer-to-layer communication.

```json
{
  "strategy": "layer_based",
  "nodes": [0, 1, 2, 3]
}
```

### Deploying an SNN

To deploy an SNN to the cluster:

1. Create a topology JSON file (see examples in `python_tools/examples/`)
2. Deploy using the `nsnn deploy` command
3. Start execution with `nsnn start`
4. Monitor activity with `nsnn monitor`

**Example:**

```bash
# Deploy the network
nsnn deploy my_network.json

# Check deployment status
nsnn status

# Start execution
nsnn start

# Monitor for 10 seconds
nsnn monitor 10000
```

### Injecting Input Spikes

To provide input to the network, create a JSON file with spike events:

```json
{
  "spikes": [
    {"neuron_id": 0, "value": 1.0},
    {"neuron_id": 1, "value": 0.8},
    {"neuron_id": 2, "value": 0.5}
  ]
}
```

Then inject the spikes:

```bash
nsnn inject input_spikes.json
```

## Advanced Topics

### Direct Memory Access

The `ncp` and `ncat` commands provide direct access to node memory, which is useful for debugging and advanced applications.

**Reading Memory:**

```bash
# Read 1KB from address 0x20000000
ncat 0@0x20000000 -l 1024 -x
```

**Writing Memory:**

```bash
# Write binary file to address 0x20000000
ncp data.bin 0@0x20000000
```

### Scripting with Python API

For advanced automation, you can use the Python API directly:

```python
from z1_client import Z1Client
import json

# Connect to cluster
client = Z1Client(controller_ip='192.168.1.222')

# List nodes
nodes = client.list_nodes()
for node in nodes:
    print(f"Node {node.node_id}: {node.status}")

# Deploy SNN
with open('network.json', 'r') as f:
    topology = json.load(f)
result = client.deploy_snn(topology)
print(f"Deployed {result['neurons_deployed']} neurons")

# Start SNN
client.start_snn()

# Monitor activity
spikes = client.get_spike_activity(duration_ms=5000)
print(f"Captured {len(spikes)} spikes")
```

### Multi-Backplane Configurations

For large-scale deployments, multiple backplanes can be networked together. Each backplane has its own controller node with a unique IP address. The Python tools can manage multiple controllers by specifying different IPs.

**Example:**

```bash
# List nodes on backplane 1
nls -c 192.168.1.222

# List nodes on backplane 2
nls -c 192.168.1.223
```

## Troubleshooting

### Connection Refused

**Problem:** Commands fail with "Connection refused" error.

**Solutions:**

1. Verify the controller IP address is correct
2. Check that the controller node is powered on
3. Verify network connectivity: `ping 192.168.1.222`
4. Check that the HTTP server is running on the controller

### Node Not Responding

**Problem:** A node doesn't respond to commands.

**Solutions:**

1. Use `nping` to test node connectivity: `nping 0`
2. Use `nls` to check node status
3. Check node LED indicators for error states
4. Try resetting the node (if reset command is implemented)

### Memory Access Errors

**Problem:** Memory read/write operations fail.

**Solutions:**

1. Verify the address is within PSRAM range (0x20000000-0x207FFFFF)
2. Check that the node has sufficient free memory using `nls -v`
3. Ensure data size doesn't exceed available memory
4. Try smaller transfer sizes for large operations

### SNN Deployment Fails

**Problem:** SNN deployment fails or produces errors.

**Solutions:**

1. Validate the topology JSON file for syntax errors
2. Check that neuron count doesn't exceed cluster capacity
3. Verify that layer neuron IDs are sequential and non-overlapping
4. Ensure connection definitions reference valid layer IDs
5. Check that the number of synapses per neuron doesn't exceed 60

### Low Spike Rate

**Problem:** The SNN shows very low or zero spike activity.

**Solutions:**

1. Check that input spikes are being injected correctly
2. Verify neuron thresholds are not too high
3. Check that weights are properly initialized
4. Ensure the leak rate is appropriate (typically 0.95)
5. Monitor membrane potentials using `ncat` with neuron table parsing

## Conclusion

This guide provides a comprehensive overview of the NeuroFab Z1 cluster management system. For additional technical details, refer to the system design documentation in `docs/system_design.md`. For questions or support, consult the NeuroFab documentation or contact the development team.
