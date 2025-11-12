# NeuroFab Z1 Neuromorphic Cluster

A complete cluster management system for the NeuroFab Z1 neuromorphic computing platform, featuring Unix-style CLI tools, embedded firmware, software emulator, and distributed SNN deployment framework.

---

## Features

- **Cluster Management** - Unix-style CLI tools for managing 200+ nodes across multiple backplanes
- **Distributed SNNs** - Deploy and execute Spiking Neural Networks across the cluster
- **STDP Learning** - Spike-Timing-Dependent Plasticity for online learning
- **Software Emulator** - Full hardware simulation for development without physical hardware
- **Remote Firmware Flashing** - Update node firmware over the network
- **Multi-Backplane Support** - Scale to hundreds of nodes

---

## Quick Start

### 1. Installation

```bash
# Clone repository
git clone https://github.com/ahtx/neurofab-z1-cluster.git
cd neurofab-z1-cluster

# Install dependencies
pip3 install requests numpy flask

# Add tools to PATH
export PATH=$PATH:$(pwd)/python_tools/bin
```

### 2. Start Emulator (Optional)

```bash
cd emulator
python3 z1_emulator.py
```

### 3. Configure Tools

```bash
# For emulator
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000

# For hardware
export Z1_CONTROLLER_IP=192.168.1.222
export Z1_CONTROLLER_PORT=80
```

### 4. Test Connection

```bash
# List all nodes
nls

# Check cluster status
nstat
```

### 5. Deploy an SNN

```bash
# Deploy XOR network
nsnn deploy python_tools/examples/xor_snn.json

# Start execution
nsnn start

# Monitor activity
nsnn monitor 5000
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [User Guide](docs/USER_GUIDE.md) | Complete guide for using the cluster management tools |
| [Developer Guide](docs/DEVELOPER_GUIDE.md) | Firmware development, building, and integration |
| [SNN Guide](docs/SNN_GUIDE.md) | Deploying and training Spiking Neural Networks |
| [API Reference](docs/API_REFERENCE.md) | REST API documentation |
| [Architecture](docs/ARCHITECTURE.md) | System architecture and design |
| [Changelog](CHANGELOG.md) | Version history and changes |

---

## Project Structure

```
neurofab-z1-cluster/
├── python_tools/          # Cluster management CLI tools
│   ├── bin/               # Executables (nls, nping, nstat, ncp, ncat, nflash, nsnn, nconfig)
│   ├── lib/               # Python libraries
│   └── examples/          # Example SNN topologies
├── embedded_firmware/     # Firmware for RP2350B nodes
│   ├── node/              # Node firmware (SNN execution engine)
│   ├── controller/        # Controller firmware (HTTP API server)
│   ├── bootloader/        # Two-stage bootloader
│   └── common/            # Shared protocol definitions
├── emulator/              # Software emulator
│   ├── core/              # Emulation engine
│   ├── tools/             # Emulator-compatible tools
│   └── examples/          # Test networks
└── docs/                  # Documentation
```

---

## CLI Tools

| Tool | Description |
|------|-------------|
| `nls` | List all compute nodes |
| `nping` | Test connectivity to nodes |
| `nstat` | Real-time cluster status |
| `ncp` | Copy data to node memory |
| `ncat` | Display node memory contents |
| `nflash` | Flash firmware to nodes |
| `nsnn` | Deploy and manage SNNs |
| `nconfig` | Cluster configuration management |

---

## Hardware Architecture

- **Processor:** Raspberry Pi RP2350B (Dual Cortex-M33 @ 150MHz)
- **Memory:** 8MB PSRAM per node
- **Communication:** Z1 matrix bus (16-bit parallel GPIO)
- **Network:** W5500 Ethernet controller (controller node)
- **Nodes per Backplane:** Up to 16 compute nodes + 1 controller
- **Scalability:** Multi-backplane support for 200+ nodes

---

## SNN Capabilities

- **Neuron Model:** Leaky Integrate-and-Fire (LIF)
- **Neurons per Node:** Up to 4,096
- **Synapses per Neuron:** Up to 60
- **Learning:** STDP (Spike-Timing-Dependent Plasticity)
- **Deployment:** Distributed across multiple nodes
- **Monitoring:** Real-time spike activity tracking

---

## Examples

### List Nodes

```bash
$ nls
NODE  STATUS    MEMORY      UPTIME
--------------------------------------------------
   0  active    7.85 MB     2h 15m
   1  active    7.85 MB     2h 15m
   2  active    7.85 MB     2h 15m
   3  active    7.85 MB     2h 15m

Total: 4 nodes
```

### Deploy SNN

```bash
$ nsnn deploy examples/mnist_snn.json
Compiling SNN topology...
  Network: MNIST_SNN_Classifier
  Neurons: 1794
  Synapses: 235200
  Nodes: 12

Deploying to cluster...
  Node 0: 150 neurons, 18750 synapses ✓
  Node 1: 150 neurons, 18750 synapses ✓
  ...

Deployment complete!
```

### Monitor Activity

```bash
$ nsnn monitor 5000
Monitoring SNN activity for 5000ms...

Spike Activity:
  Total Spikes: 15847
  Spike Rate: 3169.40 Hz
  Active Neurons: 342/1794 (19.1%)

Top 10 Most Active Neurons:
  Neuron 145: 234 spikes
  Neuron 892: 198 spikes
  Neuron 1203: 187 spikes
  ...
```

---

## Development

### Building Firmware

```bash
cd embedded_firmware
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Running Tests

```bash
# Start emulator
cd emulator
python3 z1_emulator.py &

# Run test
cd ../python_tools/examples
../bin/nsnn deploy xor_snn.json
../bin/nsnn start
../bin/nsnn monitor 1000
```

---

## Contributing

Contributions are welcome! Please see the [Developer Guide](docs/DEVELOPER_GUIDE.md) for details on the development workflow.

---

## License

This project is licensed under the MIT License.

---

## Acknowledgments

Developed for the NeuroFab Z1 neuromorphic computing platform.

---

## Support

For issues, questions, or feature requests, please visit:
- **GitHub:** https://github.com/ahtx/neurofab-z1-cluster
- **Documentation:** [docs/](docs/)

---

**Version:** 1.0  
**Release Date:** November 12, 2025  
**Status:** Production Ready
