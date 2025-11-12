# NeuroFab Z1 Cluster Management Tools

Unix-style command-line utilities for managing and programming the NeuroFab Z1 neuromorphic computing cluster.

## Overview

This toolkit provides a comprehensive set of utilities for interacting with Z1 clusters, including node management, memory operations, and Spiking Neural Network (SNN) deployment.

## Installation

### Requirements

- Python 3.7 or higher
- `requests` library

Install dependencies:

```bash
pip3 install requests
```

### Setup

Add the `bin` directory to your PATH:

```bash
export PATH=$PATH:/path/to/neurofab-cluster-tools/bin
```

Or create symlinks in `/usr/local/bin`:

```bash
sudo ln -s /path/to/neurofab-cluster-tools/bin/* /usr/local/bin/
```

## Utilities

### nconfig - Cluster Configuration

Manage multi-backplane cluster configurations.

```bash
nconfig init                           # Create new configuration
nconfig list                           # List all backplanes
nconfig add bp-0 192.168.1.222        # Add backplane
nconfig remove bp-0                    # Remove backplane
nconfig show bp-0                      # Show backplane details
```

### nls - List Nodes

List all compute nodes in the cluster. Supports multi-backplane configurations with 200+ nodes.

```bash
nls                    # List all nodes (uses default config)
nls --all              # List all nodes from all backplanes
nls --all --parallel   # Parallel queries (faster)
nls -v                 # Verbose output with memory info
nls -j                 # JSON output for scripting
nls --backplane bp-0   # List nodes from specific backplane
nls -c 192.168.1.100   # Single backplane mode (legacy)
```

### nping - Ping Nodes

Test connectivity to cluster nodes.

```bash
nping 0                # Ping node 0
nping all              # Ping all nodes
nping 0 -n 10          # Ping node 0 ten times
```

### nstat - Cluster Status

Display cluster status and statistics.

```bash
nstat                  # Show status once
nstat -w 1             # Live monitoring (refresh every 1 second)
nstat -s               # Show SNN activity statistics
```

### ncp - Copy to Nodes

Copy files to node memory.

```bash
ncp weights.bin 0/weights           # Copy to node 0 weights location
ncp firmware.bin 0@0x20000000       # Copy to specific address
ncp data.bin 0/data -v              # Copy with progress
```

**Named Memory Locations:**
- `weights` - 0x20000000 (start of PSRAM)
- `neurons` - 0x20100000 (1MB offset)
- `code` - 0x20200000 (2MB offset)
- `data` - 0x20300000 (3MB offset)
- `scratch` - 0x20700000 (7MB offset)

### ncat - Display Node Data

Read and display node memory contents.

```bash
ncat 0/weights         # Display weights table
ncat 0/weights -x      # Hex dump
ncat 0@0x20000000      # Display from specific address
ncat 0/neurons -n      # Parse as neuron table
```

### nsnn - SNN Management

Manage Spiking Neural Networks on the cluster.

```bash
nsnn deploy network.json           # Deploy SNN topology
nsnn status                        # Show SNN status
nsnn start                         # Start SNN execution
nsnn stop                          # Stop SNN execution
nsnn monitor 5000                  # Monitor for 5 seconds
nsnn inject input_spikes.json      # Inject input pattern
```

## Configuration

### Single Backplane Mode

All utilities accept a `-c` or `--controller` option to specify the controller node IP address. The default is `192.168.1.222`.

```bash
nls -c 192.168.1.100   # Use custom controller IP
```

### Multi-Backplane Mode

For clusters with multiple backplanes (200+ nodes), create a cluster configuration file:

```bash
# Create configuration
nconfig init -o ~/.neurofab/cluster.json

# Add backplanes
nconfig add backplane-0 192.168.1.222 --nodes 16 --description "Rack 1, Slot 1"
nconfig add backplane-1 192.168.1.223 --nodes 16 --description "Rack 1, Slot 2"
nconfig add backplane-2 192.168.1.224 --nodes 16 --description "Rack 1, Slot 3"

# List all configured backplanes
nconfig list

# Use with utilities
nls --all --parallel              # List all nodes from all backplanes
nls --backplane backplane-0       # List nodes from specific backplane
```

See `docs/multi_backplane_guide.md` for detailed multi-backplane documentation.

## SNN Topology Format

SNN topologies are defined in JSON format. See `examples/mnist_snn.json` for a complete example.

### Basic Structure

```json
{
  "network_name": "my_network",
  "neuron_count": 1000,
  "layers": [
    {
      "layer_id": 0,
      "layer_type": "input",
      "neuron_count": 100,
      "neuron_ids": [0, 99],
      "threshold": 1.0
    }
  ],
  "connections": [
    {
      "source_layer": 0,
      "target_layer": 1,
      "connection_type": "fully_connected",
      "weight_init": "random_normal"
    }
  ],
  "node_assignment": {
    "strategy": "balanced",
    "nodes": [0, 1, 2, 3]
  }
}
```

### Layer Types

- `input` - Input layer (no incoming connections)
- `hidden` - Hidden layer with LIF neurons
- `output` - Output layer

### Connection Types

- `fully_connected` - Every neuron in source connects to every neuron in target
- `sparse_random` - Random sparse connectivity
- `convolutional` - Convolutional connectivity pattern

### Weight Initialization

- `random_normal` - Normal distribution (specify mean and stddev)
- `random_uniform` - Uniform distribution (specify min and max)
- `constant` - Constant value

## Python API

The utilities are built on the `z1_client` library, which can be used directly in Python scripts:

```python
from z1_client import Z1Client

# Connect to cluster
client = Z1Client(controller_ip='192.168.1.222')

# List nodes
nodes = client.list_nodes()
for node in nodes:
    print(f"Node {node.node_id}: {node.status}")

# Read memory
data = client.read_memory(node_id=0, addr=0x20000000, length=256)

# Deploy SNN
with open('network.json', 'r') as f:
    topology = json.load(f)
result = client.deploy_snn(topology)
```

## Examples

### Deploy and Run MNIST Classifier

```bash
# Deploy the network
nsnn deploy examples/mnist_snn.json

# Check deployment status
nsnn status

# Start execution
nsnn start

# Monitor activity
nsnn monitor 5000

# Stop execution
nsnn stop
```

### Copy Weights to Multiple Nodes

```bash
# Copy same weights to nodes 0-3
for i in {0..3}; do
    ncp weights.bin $i/weights
done
```

### Live Cluster Monitoring

```bash
# Watch cluster status with 1-second refresh
nstat -w 1 -s
```

### Memory Inspection

```bash
# Hex dump of neuron table
ncat 0/neurons -x -l 1024

# Parse neuron entries
ncat 0/neurons -n

# Binary dump to file
ncat 0/data -b > node0_data.bin
```

## Architecture

The toolkit communicates with the Z1 cluster via HTTP REST API exposed by the controller node. The controller translates API requests into Z1 bus operations, which are executed on the compute nodes.

```
[Python Utilities] --HTTP--> [Controller Node] --Z1 Bus--> [Compute Nodes]
```

### Communication Flow

1. Utility makes HTTP request to controller API endpoint
2. Controller receives request and validates parameters
3. Controller sends bus commands to target node(s)
4. Node(s) execute commands and respond via bus
5. Controller aggregates responses and returns HTTP response
6. Utility processes and displays results

## Troubleshooting

### Connection Refused

If you get "Connection refused" errors:

1. Verify controller IP address is correct
2. Check that controller node is powered on
3. Verify network connectivity: `ping 192.168.1.222`
4. Check that HTTP server is running on controller

### Node Not Responding

If a node doesn't respond to commands:

1. Use `nping` to test node connectivity
2. Use `ndiscover` to scan for active nodes
3. Check node LED indicators for error states
4. Try resetting the node: `nreset <node_id>`

### Memory Access Errors

If memory operations fail:

1. Verify address is within PSRAM range (0x20000000-0x207FFFFF)
2. Check that node has sufficient free memory
3. Ensure data size doesn't exceed available memory
4. Try smaller transfer sizes for large operations

## Development

### Adding New Utilities

1. Create new script in `bin/` directory
2. Import `z1_client` library
3. Follow Unix conventions for arguments and exit codes
4. Add documentation to this README

### Extending the API

To add new API endpoints:

1. Update `z1_client.py` with new methods
2. Implement corresponding endpoints in embedded server
3. Add new bus commands if needed
4. Update documentation

## License

Copyright NeuroFab Corp. All rights reserved.

## Support

For technical support and documentation, visit the NeuroFab website or contact the development team.
