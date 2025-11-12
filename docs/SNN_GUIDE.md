# Deploying SNNs on the Z1 Cluster: A Complete Tutorial

**Author:** NeuroFab  
**Date:** November 11, 2025

## Introduction

This tutorial provides a complete, step-by-step guide to deploying and running Spiking Neural Networks (SNNs) on the NeuroFab Z1 neuromorphic cluster. We'll start with a simple example and progress to deploying large networks across hundreds of nodes.

### What You'll Learn

- How the Z1 cluster architecture works
- How to define SNN topologies in JSON format
- How neurons are distributed across compute nodes
- How to deploy, start, and monitor SNNs
- How to scale to 200+ nodes across multiple backplanes

### Prerequisites

- Z1 cluster with at least 2 compute nodes
- Python 3.7+ with `requests` and `numpy` libraries
- Basic understanding of neural networks
- Network connectivity to controller node(s)

## Part 1: Understanding the Architecture

### The Big Picture

The Z1 cluster is a distributed neuromorphic computing system designed for massively parallel, event-driven computation using Spiking Neural Networks.

```
┌─────────────────────────────────────────────────────────────┐
│                      YOUR WORKSTATION                       │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │   nls    │  │  nconfig │  │   nsnn   │  │   ncat   │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │
│       └─────────────┴─────────────┴─────────────┘         │
│                          │                                  │
│                    Python Tools                             │
│                   (HTTP/REST API)                           │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           │ Ethernet
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    CONTROLLER NODE                          │
│                  (RP2350B + W5500)                          │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           HTTP REST API Server (Port 80)             │  │
│  │  - /api/nodes - /api/snn/deploy - /api/snn/start    │  │
│  └────────────────────────┬─────────────────────────────┘  │
│                           │                                 │
│                    Z1 Bus Protocol                          │
│                    (16-bit parallel)                        │
└───────────────────────────┬─────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│ COMPUTE NODE 0│   │ COMPUTE NODE 1│   │ COMPUTE NODE N│
│  (RP2350B)    │   │  (RP2350B)    │   │  (RP2350B)    │
│               │   │               │   │               │
│ ┌───────────┐ │   │ ┌───────────┐ │   │ ┌───────────┐ │
│ │ 8MB PSRAM │ │   │ │ 8MB PSRAM │ │   │ │ 8MB PSRAM │ │
│ │           │ │   │ │           │ │   │ │           │ │
│ │ Neuron    │ │   │ │ Neuron    │ │   │ │ Neuron    │ │
│ │ Table     │ │   │ │ Table     │ │   │ │ Table     │ │
│ │ (4096     │ │   │ │ (4096     │ │   │ │ (4096     │ │
│ │  neurons) │ │   │ │  neurons) │ │   │ │  neurons) │ │
│ └───────────┘ │   │ └───────────┘ │   │ └───────────┘ │
│               │   │               │   │               │
│ SNN Engine    │   │ SNN Engine    │   │ SNN Engine    │
│ (LIF neurons) │   │ (LIF neurons) │   │ (LIF neurons) │
└───────────────┘   └───────────────┘   └───────────────┘
```

### How It Works: The Complete Flow

**1. You Define the Network (JSON Topology)**

You create a JSON file describing your SNN: layers, neurons, connections, and weights.

**2. The Compiler Distributes Neurons**

The `snn_compiler` takes your topology and:
- Assigns neurons to specific compute nodes
- Generates synaptic connection tables
- Creates binary neuron tables (256 bytes per neuron)
- Produces a deployment plan

**3. Deployment to Cluster**

The `nsnn deploy` command:
- Uploads neuron tables to each node's PSRAM (address 0x20100000)
- Configures each node with its neuron count
- Stores the deployment plan for later use

**4. Execution**

When you run `nsnn start`:
- Each node loads its neuron table from PSRAM
- The SNN engine begins processing at 1kHz (1ms timesteps)
- Neurons integrate incoming spikes and fire when threshold is exceeded
- Spikes propagate across the Z1 bus to target neurons

**5. Monitoring**

You can observe the network with `nsnn monitor`:
- Captures spike events from all nodes
- Calculates spike rates and activity statistics
- Shows which neurons are firing

## Part 2: Your First SNN - XOR Logic Gate

Let's start with the simplest possible example: implementing an XOR logic gate as an SNN.

### Step 1: Understand the XOR Problem

XOR (exclusive OR) is a classic non-linearly separable problem:

```
Input A  Input B  Output
   0        0       0
   0        1       1
   1        0       1
   1        1       0
```

We'll implement this with 7 neurons:
- 2 input neurons (A and B)
- 4 hidden neurons (for non-linear processing)
- 1 output neuron (result)

### Step 2: Examine the Topology File

The XOR topology is already provided in `examples/xor_snn.json`. Let's understand its structure:

```json
{
  "network_name": "XOR_SNN_Example",
  "neuron_count": 7,
  "layers": [
    {
      "layer_id": 0,
      "layer_type": "input",
      "neuron_count": 2,
      "neuron_ids": [0, 1],
      "threshold": 1.0,
      "leak_rate": 0.0
    },
    {
      "layer_id": 1,
      "layer_type": "hidden",
      "neuron_count": 4,
      "neuron_ids": [2, 5],
      "threshold": 1.0,
      "leak_rate": 0.95,
      "refractory_period_us": 1000
    },
    {
      "layer_id": 2,
      "layer_type": "output",
      "neuron_count": 1,
      "neuron_ids": [6, 6],
      "threshold": 1.5,
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
      "weight_mean": 0.6,
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
    "nodes": [0, 1]
  }
}
```

**Key Points:**

- **Layers**: Input (2), Hidden (4), Output (1)
- **Neuron IDs**: Globally unique (0-6)
- **Connections**: Fully connected between layers
- **Node Assignment**: Balanced across nodes 0 and 1

### Step 3: Deploy the XOR Network

```bash
# Deploy to the cluster
nsnn deploy examples/xor_snn.json
```

**What Happens:**

```
Loading topology: examples/xor_snn.json
Compiling SNN topology...

Deployment Plan:
  Total Neurons:  7
  Total Synapses: 12
  Backplanes:     1
  Nodes:          2

Distribution:
  default: 2 nodes, 7 neurons

Deploying neuron tables...

  Deploying to default (192.168.1.222)...
    Node  0:    4 neurons (  1024 bytes) ✓
    Node  1:    3 neurons (   768 bytes) ✓

Deployment Summary:
  Successful: 2/2 nodes
  Total neurons deployed: 7

✓ SNN deployed successfully!
```

**Behind the Scenes:**

1. **Compilation**: The compiler assigns neurons 0-3 to node 0, neurons 4-6 to node 1
2. **Neuron Tables**: Each neuron becomes a 256-byte entry with state, parameters, and synapses
3. **Memory Upload**: Tables are written to PSRAM at 0x20100000
4. **Deployment Info**: Saved to `~/.neurofab/last_deployment.json` for later reference

### Step 4: Check Deployment Status

```bash
nsnn status
```

**Output:**

```
SNN Status:
============================================================
  Topology:       xor_snn.json
  Total Neurons:  7
  Total Synapses: 12
  Backplanes:     1

Distribution:
  default: 2 nodes
```

### Step 5: Start the SNN

```bash
nsnn start
```

**Output:**

```
Starting SNN execution...
  default: Started ✓

✓ SNN started
```

**What Happens on Each Node:**

1. **Initialization**: SNN engine loads neuron table from PSRAM
2. **Main Loop**: Begins 1kHz processing loop
3. **Spike Processing**: Listens for incoming spikes on Z1 bus
4. **LIF Integration**: Updates membrane potentials each timestep
5. **Spike Generation**: Fires spikes when threshold exceeded
6. **Spike Transmission**: Sends spikes to target neurons via bus

### Step 6: Inject Input Pattern

Let's test XOR with input (0, 1), which should produce output 1:

```bash
nsnn inject examples/xor_input_01.json
```

**Output:**

```
Loading spike pattern: examples/xor_input_01.json
Injecting 2 spikes...
  default: 2 spikes injected ✓

✓ Spikes injected
```

**What Happens:**

1. Input neurons 0 and 1 receive spike values (0.0, 1.0)
2. Neuron 1 fires because its input exceeds threshold
3. Spike propagates to hidden layer neurons (2-5)
4. Hidden neurons integrate the spike, some fire
5. Output neuron 6 integrates hidden layer spikes
6. If total input > 1.5, neuron 6 fires (XOR = 1)

### Step 7: Monitor Activity

```bash
nsnn monitor 5000
```

**Output:**

```
Monitoring spike activity for 5000ms...
  default: 47 spikes

Total spikes captured: 47
Spike rate: 9.40 Hz
```

### Step 8: Test All XOR Patterns

```bash
# Test 0 XOR 0 = 0
nsnn inject examples/xor_input_00.json
nsnn monitor 2000

# Test 0 XOR 1 = 1
nsnn inject examples/xor_input_01.json
nsnn monitor 2000

# Test 1 XOR 0 = 1
nsnn inject examples/xor_input_10.json
nsnn monitor 2000

# Test 1 XOR 1 = 0
nsnn inject examples/xor_input_11.json
nsnn monitor 2000
```

### Step 9: Stop the SNN

```bash
nsnn stop
```

**Output:**

```
Stopping SNN execution...
  default: Stopped ✓

✓ SNN stopped
```

## Part 3: Scaling to 200+ Nodes - MNIST Classifier

Now let's deploy a real-world SNN: an MNIST digit classifier with 1,794 neurons across 12 backplanes (192 nodes).

### The MNIST SNN Architecture

```
Input Layer:    784 neurons (28×28 pixel image)
Hidden Layer:  1000 neurons (feature detection)
Output Layer:    10 neurons (digit 0-9)
Total:         1794 neurons, 784,000 synapses
```

### Step 1: Configure Multi-Backplane Cluster

```bash
# Create cluster configuration
nconfig init -o ~/.neurofab/cluster.json

# Add 12 backplanes (192 nodes total)
for i in {0..11}; do
    ip="192.168.1.$((222 + i))"
    nconfig add "backplane-$i" "$ip" --nodes 16 \
        --description "Rack $((i/4 + 1)), Slot $((i%4 + 1))"
done

# Verify configuration
nconfig list
```

**Output:**

```
Cluster Configuration: /home/user/.neurofab/cluster.json
================================================================================
NAME             CONTROLLER IP    NODES  DESCRIPTION
--------------------------------------------------------------------------------
backplane-0      192.168.1.222    16     Rack 1, Slot 1
backplane-1      192.168.1.223    16     Rack 1, Slot 2
...
backplane-11     192.168.1.233    16     Rack 3, Slot 4

Total: 12 backplane(s), 192 nodes
```

### Step 2: Deploy MNIST SNN Across All Backplanes

```bash
nsnn deploy examples/mnist_snn.json --all
```

**Output:**

```
Loading topology: examples/mnist_snn.json
Compiling SNN topology...

Deployment Plan:
  Total Neurons:  1794
  Total Synapses: 784000
  Backplanes:     12
  Nodes:          192

Distribution:
  backplane-0: 16 nodes, 150 neurons
  backplane-1: 16 nodes, 150 neurons
  backplane-2: 16 nodes, 150 neurons
  ...
  backplane-11: 16 nodes, 144 neurons

Deploying neuron tables...

  Deploying to backplane-0 (192.168.1.222)...
    Node  0:   10 neurons (  2560 bytes) ✓
    Node  1:   10 neurons (  2560 bytes) ✓
    ...
    Node 15:   10 neurons (  2560 bytes) ✓

  Deploying to backplane-1 (192.168.1.223)...
    Node  0:   10 neurons (  2560 bytes) ✓
    ...

  Deploying to backplane-11 (192.168.1.233)...
    ...
    Node 15:    9 neurons (  2304 bytes) ✓

Deployment Summary:
  Successful: 192/192 nodes
  Total neurons deployed: 1794

✓ SNN deployed successfully!
```

**What Just Happened:**

1. **Compiler**: Distributed 1,794 neurons across 192 nodes (~9 neurons per node)
2. **Connections**: Generated 784,000 synapses (input→hidden fully connected)
3. **Upload**: Sent neuron tables to all 192 nodes across 12 backplanes
4. **Parallel**: Used concurrent HTTP requests to deploy to all backplanes simultaneously

### Step 3: Start MNIST Network

```bash
nsnn start
```

**Output:**

```
Starting SNN execution...
  backplane-0: Started ✓
  backplane-1: Started ✓
  ...
  backplane-11: Started ✓

✓ SNN started
```

**Now Running:**

- 192 compute nodes executing in parallel
- 1,794 neurons processing spikes
- 784,000 synaptic connections active
- Distributed across 12 backplanes

### Step 4: Inject MNIST Digit Image

```bash
# Inject a handwritten "7" image
nsnn inject examples/mnist_digit_7.json
```

**What Happens:**

1. **Input Encoding**: 28×28 pixel values → 784 spike rates
2. **Spike Injection**: Spikes sent to input layer neurons (0-783)
3. **Propagation**: Spikes travel across backplanes to hidden layer
4. **Processing**: 1,000 hidden neurons integrate and fire
5. **Classification**: 10 output neurons compete, highest activity = predicted digit

### Step 5: Monitor Classification

```bash
nsnn monitor 100
```

**Output:**

```
Monitoring spike activity for 100ms...
  backplane-0: 1247 spikes
  backplane-1: 1189 spikes
  ...
  backplane-11: 1203 spikes

Total spikes captured: 14523
Spike rate: 145230.00 Hz
```

### Step 6: Analyze Results

The output neurons (1784-1793) represent digits 0-9. The neuron with the highest spike count indicates the predicted digit.

```bash
# Extract output neuron activity (would need additional tooling)
# Expected: Neuron 1791 (digit 7) has highest spike count
```

## Part 4: How the System Works Together

### Component Interaction Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    USER COMMAND: nsnn deploy                    │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                  PYTHON: nsnn utility (bin/nsnn)                │
│                                                                 │
│  1. Parse command-line arguments                               │
│  2. Load topology JSON file                                    │
│  3. Load cluster configuration (if --all)                      │
│  4. Call snn_compiler.compile_snn_topology()                   │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│              PYTHON: SNN Compiler (lib/snn_compiler.py)         │
│                                                                 │
│  1. Assign neurons to nodes (balanced/layer-based)             │
│     - Input: 1794 neurons, 192 nodes                           │
│     - Output: Node assignments (9-10 neurons per node)         │
│                                                                 │
│  2. Build neuron configurations                                │
│     - For each neuron: create NeuronConfig with parameters     │
│     - Store global_id → (backplane, node, local_id) mapping    │
│                                                                 │
│  3. Generate connections                                       │
│     - Fully connected: input→hidden (784×1000 = 784k synapses) │
│     - Fully connected: hidden→output (1000×10 = 10k synapses)  │
│     - Each synapse: (source_global_id, weight_8bit)            │
│                                                                 │
│  4. Compile neuron tables                                      │
│     - For each node: pack neurons into 256-byte entries        │
│     - Format: [state|metadata|params|synapses]                 │
│     - Output: Binary neuron table per node                     │
│                                                                 │
│  5. Build deployment plan                                      │
│     - neuron_tables: (backplane, node) → binary_data           │
│     - neuron_map: global_id → (backplane, node, local_id)      │
│     - backplane_nodes: backplane → [node_ids]                  │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                  PYTHON: nsnn deploy function                   │
│                                                                 │
│  For each backplane in deployment_plan:                        │
│    1. Get controller IP from cluster config                    │
│    2. Create Z1Client(controller_ip)                           │
│    3. For each node on backplane:                              │
│       - Get neuron table from deployment_plan                  │
│       - Call client.write_memory(node_id, 0x20100000, data)    │
│    4. Report success/failure                                   │
│                                                                 │
│  Save deployment info to ~/.neurofab/last_deployment.json      │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│              PYTHON: Z1 Client (lib/z1_client.py)               │
│                                                                 │
│  write_memory(node_id, addr, data):                            │
│    1. Encode data as base64                                    │
│    2. POST /api/nodes/{node_id}/memory                         │
│       Body: {"addr": 0x20100000, "data": "<base64>"}           │
│    3. Return bytes_written                                     │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ HTTP POST
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│           CONTROLLER: HTTP API Server (z1_http_api.c)           │
│                                                                 │
│  Endpoint: POST /api/nodes/{node_id}/memory                    │
│                                                                 │
│  1. Parse JSON request body                                    │
│  2. Decode base64 data                                         │
│  3. Validate node_id (0-15)                                    │
│  4. Validate address (0x20000000-0x207FFFFF)                   │
│  5. Split data into chunks (if > 4KB)                          │
│  6. For each chunk:                                            │
│     - Send Z1_CMD_MEMORY_WRITE to node via bus                 │
│     - Wait for ACK                                             │
│  7. Return success response                                    │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ Z1 Bus Protocol
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│              COMPUTE NODE: Firmware (node.c)                    │
│                                                                 │
│  Main loop:                                                    │
│    while (1) {                                                 │
│      if (bus_message_available()) {                            │
│        cmd = bus_read_command();                               │
│        switch (cmd) {                                          │
│          case Z1_CMD_MEMORY_WRITE:                             │
│            addr = bus_read_uint32();                           │
│            len = bus_read_uint16();                            │
│            bus_read_data(buffer, len);                         │
│            memcpy((void*)addr, buffer, len);  // Write to PSRAM│
│            bus_send_ack();                                     │
│            break;                                              │
│          case Z1_CMD_SNN_START:                                │
│            z1_snn_start();                                     │
│            break;                                              │
│          case Z1_CMD_SNN_SPIKE:                                │
│            z1_snn_process_spike(...);                          │
│            break;                                              │
│        }                                                       │
│      }                                                         │
│                                                                │
│      if (snn_running) {                                        │
│        z1_snn_step(1000);  // 1ms timestep                     │
│      }                                                         │
│    }                                                           │
└─────────────────────────────────────────────────────────────────┘
```

### Neuron Table Format in PSRAM

Each neuron occupies exactly 256 bytes:

```
Offset  Size  Field                  Description
------  ----  ---------------------  ----------------------------------
0x00    2     neuron_id              Local ID (0-4095)
0x02    2     flags                  ACTIVE|INHIBITORY|INPUT|OUTPUT
0x04    4     membrane_potential     Current potential (float)
0x08    4     threshold              Spike threshold (float)
0x0C    4     last_spike_time_us     Timestamp of last spike
0x10    2     synapse_count          Number of synapses (0-60)
0x12    2     synapse_capacity       Max synapses (60)
0x14    4     reserved
0x18    4     leak_rate              Membrane leak (float, 0.0-1.0)
0x1C    4     refractory_period_us   Refractory period
0x20    8     reserved
0x28    240   synapses[60]           Synapse array (60 × 4 bytes)

Synapse format (32 bits):
  Bits [31:8]  - Source neuron global ID (24 bits)
  Bits [7:0]   - Weight (8-bit, 0-255)
```

### SNN Execution Flow

**Initialization (z1_snn_start):**

```c
bool z1_snn_start(void) {
    // Load neuron table from PSRAM
    g_snn_state.neuron_table_addr = 0x20100000;
    g_snn_state.neuron_count = /* from deployment */;
    g_snn_state.running = true;
    g_snn_state.current_time_us = 0;
    return true;
}
```

**Main Processing Loop (z1_snn_step):**

```c
void z1_snn_step(uint32_t timestep_us) {
    g_snn_state.current_time_us += timestep_us;
    
    // Process all neurons
    for (int i = 0; i < g_snn_state.neuron_count; i++) {
        z1_neuron_entry_t *neuron = get_neuron(i);
        
        // Skip if in refractory period
        if (neuron->state.flags & Z1_NEURON_FLAG_REFRACTORY) {
            if (g_snn_state.current_time_us - neuron->state.last_spike_time_us 
                > neuron->refractory_period_us) {
                neuron->state.flags &= ~Z1_NEURON_FLAG_REFRACTORY;
            }
            continue;
        }
        
        // Apply leak
        neuron->state.membrane_potential *= neuron->leak_rate;
        
        // Check for spike
        if (neuron->state.membrane_potential >= neuron->state.threshold) {
            // Fire spike
            neuron->state.last_spike_time_us = g_snn_state.current_time_us;
            neuron->state.membrane_potential = 0.0;
            neuron->state.flags |= Z1_NEURON_FLAG_REFRACTORY;
            
            // Send spike to all target neurons
            send_spike_to_targets(neuron);
            
            g_snn_state.spikes_generated++;
        }
    }
}
```

**Spike Processing (z1_snn_process_spike):**

```c
void z1_snn_process_spike(uint32_t source_global_id, float weight) {
    // Find neurons with synapses from this source
    for (int i = 0; i < g_snn_state.neuron_count; i++) {
        z1_neuron_entry_t *neuron = get_neuron(i);
        
        // Check all synapses
        for (int j = 0; j < neuron->synapse_count; j++) {
            uint32_t synapse = neuron->synapses[j];
            uint32_t source_id = (synapse >> 8) & 0xFFFFFF;
            uint8_t weight_8bit = synapse & 0xFF;
            
            if (source_id == source_global_id) {
                // Apply synaptic weight
                float weight_float = weight_8bit / 255.0;
                neuron->state.membrane_potential += weight_float;
            }
        }
    }
}
```

## Part 5: Best Practices

### Network Design

**1. Start Small**

Begin with small networks (< 100 neurons) to verify functionality before scaling.

**2. Layer Sizing**

- Input layer: Match your input dimensionality
- Hidden layers: 2-5× input size for good feature extraction
- Output layer: Number of classes

**3. Connection Patterns**

- Fully connected: Good for small networks, expensive for large
- Sparse random: More biologically realistic, scales better
- Convolutional: Best for spatial data (images)

### Deployment Strategy

**1. Balanced Assignment**

Use `"strategy": "balanced"` to evenly distribute neurons across all nodes. This maximizes parallelism.

**2. Layer-Based Assignment**

Use `"strategy": "layer_based"` if layers need to be kept together (e.g., for easier debugging).

**3. Multi-Backplane**

For networks > 1000 neurons, use multiple backplanes to leverage more compute power.

### Performance Optimization

**1. Minimize Cross-Node Connections**

Connections within a node are faster than cross-node. The compiler tries to minimize this automatically.

**2. Use Appropriate Timesteps**

- 1ms (1000μs): Good default
- 100μs: High-precision timing
- 10ms: Slower dynamics, less computation

**3. Monitor Spike Rates**

Very high spike rates (>100kHz) may saturate the bus. Adjust thresholds or weights if needed.

## Part 6: Troubleshooting

### Problem: Deployment Fails on Some Nodes

**Symptoms:**

```
Deployment Summary:
  Successful: 10/12 nodes
```

**Solutions:**

1. Check node connectivity: `nping all`
2. Verify PSRAM is working: `ncat 0@0x20000000 -l 256`
3. Try deploying to individual nodes: `nsnn deploy network.json -c 192.168.1.222`

### Problem: No Spike Activity

**Symptoms:**

```
Total spikes captured: 0
```

**Solutions:**

1. Verify SNN is running: `nsnn status`
2. Check input injection: `nsnn inject input.json`
3. Lower neuron thresholds in topology
4. Increase input spike values

### Problem: Too Many Spikes

**Symptoms:**

```
Spike rate: 500000.00 Hz
```

**Solutions:**

1. Increase neuron thresholds
2. Reduce synaptic weights
3. Increase leak rate (0.95 → 0.90)
4. Add refractory periods

## Conclusion

You now have a complete understanding of how to deploy and run SNNs on the Z1 neuromorphic cluster:

- **Architecture**: How the controller, nodes, and tools work together
- **Topology**: How to define networks in JSON format
- **Deployment**: How neurons are distributed and uploaded
- **Execution**: How the SNN engine processes spikes
- **Scaling**: How to use 200+ nodes across multiple backplanes

### Next Steps

1. **Experiment**: Try different network architectures
2. **Optimize**: Tune weights and parameters for your application
3. **Scale**: Deploy larger networks across more backplanes
4. **Integrate**: Connect to sensors or other systems

### Additional Resources

- **API Reference**: `docs/api_reference.md`
- **User Guide**: `docs/user_guide.md`
- **Multi-Backplane Guide**: `docs/multi_backplane_guide.md`
- **System Design**: `docs/system_design.md`

Happy neuromorphic computing!
