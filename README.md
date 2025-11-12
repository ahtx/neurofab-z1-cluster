# NeuroFab Z1 Neuromorphic Cluster System

**Author:** NeuroFab
**Date:** Nov 11, 2025

## Introduction

This project provides a comprehensive suite of software for managing, programming, and deploying Spiking Neural Networks (SNNs) on the NeuroFab Z1 neuromorphic computing cluster. The system is designed to provide a powerful yet intuitive interface for researchers and developers working with event-based, parallel computing architectures.

This document serves as the central hub for the entire system, providing an overview of the architecture and linking to detailed documentation for each component.

## System Architecture

The Z1 cluster management system is composed of three primary layers that work in concert to provide a seamless user experience:

1.  **Python Cluster Management Tools**: A collection of command-line utilities that provide a familiar, Unix-style interface for interacting with the cluster. These tools communicate with the controller node over a standard Ethernet network using an HTTP-based REST API.

2.  **Embedded HTTP Server**: A lightweight web server running on the controller node of each Z1 backplane. It exposes a RESTful API that translates high-level HTTP requests into low-level commands on the Z1's internal matrix bus.

3.  **SNN Deployment & Execution Framework**: A powerful framework that runs on the compute nodes. It includes a Leaky Integrate-and-Fire (LIF) neuron execution engine, a message-based communication system for spike propagation, and mechanisms for loading neuron configurations and updating synaptic weights.

The architecture is designed for scalability, allowing multiple Z1 backplanes to be networked together to form a large-scale neuromorphic fabric.

```
+--------------------------+      +------------------------+      +-----------------------+
|   Python CLI Utilities   |      |   Controller Node      |      |    Compute Nodes (x16)  |
| (nls, ncp, nsnn, etc.)   |      | (RP2350B)              |      |    (RP2350B)          |
+--------------------------+      +------------------------+      +-----------------------+
|        User PC           |      | |                      |      | |                     |
|                          |      | +-> HTTP REST API      |      | +-> SNN Engine        |
|                          |      | |   (W5500 Ethernet)   |      | |   (LIF Neurons)     |
+--------------------------+      | |                      |      | |                     |
             ^                    | +-> Z1 Bus Protocol    |      | +-> Spike Processing  |
             |                    | |   (Extended Commands)  |      | |                     |
             |                    +------------------------+      +-----------------------+
             |                                 ^                           ^
             +------------HTTP/TCP------------+                           |
                                                 |                           |
                                                 +----Z1 Matrix Bus (GPIO)---+
```

## Components

This repository is organized into three main sections:

### 1. Documentation (`docs/`)

This directory contains all the design and architecture documents for the system.

- **`system_design.md`**: The complete system architecture, API specification, bus protocol extensions, and data structure definitions.
- **`neurofab_findings.md`**: The initial analysis and findings from studying the original hardware documents and code.

### 2. Python Cluster Management Tools (`python_tools/`)

This directory contains the Python-based command-line utilities for managing the cluster. These tools are designed to be run from a developer's workstation.

- **`bin/`**: Executable scripts for each command (`nls`, `ncp`, `ncat`, `nping`, `nstat`, `nsnn`, `nconfig`, `nflash`).
- **`lib/`**: Core Python libraries, including the `z1_client.py` API client and the `snn_compiler.py`.
- **`examples/`**: Example files, including a sample SNN topology for MNIST classification (`mnist_snn.json`).

For detailed usage instructions, see the [Python Tools README](./python_tools/README.md).

### 3. Embedded Firmware (`embedded_firmware/`)

This directory contains the C code designed to run on the RP2350B microcontrollers within the Z1 cluster.

- **`common/`**: Code shared between the controller and compute nodes, such as the extended bus protocol definition.
- **`controller/`**: Source code for the HTTP REST API server that runs on the controller node.
- **`node/`**: Source code for the SNN execution engine that runs on the compute nodes.

For detailed information on the firmware architecture and build process, see the [Embedded Firmware README](./embedded_firmware/README.md).

## Getting Started

1.  **Review the Documentation**: Start by reading the `docs/system_design.md` to understand the overall architecture and API.

2.  **Set up the Python Tools**: Follow the instructions in `python_tools/README.md` to install the CLI utilities on your development machine.

3.  **Compile and Flash the Firmware**: Follow the instructions in `embedded_firmware/README.md` to integrate the new code with the existing Z1 firmware, compile it, and flash it to the controller and compute nodes.

4.  **Test the Cluster**: Use the `nls` and `nping` utilities to verify that the cluster is online and all nodes are responding.

    ```bash
    # List all nodes
    nls

    # Ping all nodes
    nping all
    ```

5.  **Configure Multi-Backplane Cluster** (optional): If you have multiple backplanes, create a cluster configuration.

    ```bash
    # Create cluster configuration
    nconfig init -o ~/.neurofab/cluster.json

    # Add backplanes
    nconfig add backplane-0 192.168.1.222 --nodes 16
    nconfig add backplane-1 192.168.1.223 --nodes 16

    # List all nodes from all backplanes
    nls --all --parallel
    ```

6.  **Deploy an SNN**: Use the `nsnn` utility to deploy and run the example SNN.

    ```bash
    # Deploy the MNIST SNN topology
    nsnn deploy python_tools/examples/mnist_snn.json

    # Start the SNN simulation
    nsnn start

    # Monitor spike activity for 5 seconds
    nsnn monitor 5000
    ```

## Key Features

- **Unix-Style CLI**: Intuitive and scriptable command-line tools for cluster management.
- **Multi-Backplane Support**: Manage 200+ nodes across multiple controller boards from a single interface.
- **Parallel Queries**: Query all backplanes simultaneously for fast cluster-wide operations.
- **RESTful API**: A clean, modern API for programmatic access to the cluster.
- **SNN Deployment**: A compiler that translates high-level JSON topology definitions into optimized, distributable neuron tables.
- **Dynamic Memory Access**: Tools to directly read from and write to the memory of any compute node for debugging and dynamic configuration (`ncat`, `ncp`).
- **Real-Time Monitoring**: Utilities to monitor cluster status and SNN spike activity in real-time (`nstat`, `nsnn monitor`).
- **Scalable Architecture**: Designed to manage everything from a single backplane (16 nodes) to a full multi-rack system (200+ nodes).

## 🔧 Firmware Architecture

The Z1 system uses a **two-stage bootloader architecture** that allows you to flash custom firmware to compute nodes remotely:

- **Bootloader (16KB, protected)** - Handles firmware updates, provides API to applications
- **Application Firmware (112KB, updatable)** - Your custom code (SNN, matrix ops, signal processing, etc.)
- **Remote Flashing** - Update firmware over the network with `nflash` utility

This means you can:
- Run different algorithms on different nodes
- Update firmware without physical access
- Experiment rapidly with new algorithms
- Always recover via protected bootloader

**Example:**
```bash
# Flash custom firmware to all nodes
nflash flash my_algorithm.bin all --name "My Algorithm" --version "1.0.0"

# Check firmware info
nflash info all
```

See **[Firmware Development Guide](docs/firmware_development_guide.md)** for complete details on creating custom firmware.

## License

Copyright © 2025 NeuroFab Corp. All Rights Reserved.

## 📚 New: Complete SNN Tutorial

We've added a comprehensive, step-by-step tutorial that walks you through deploying SNNs on the Z1 cluster:

**[SNN Tutorial: From XOR to MNIST](docs/snn_tutorial.md)**

This tutorial covers:
- Understanding the Z1 architecture
- Deploying your first SNN (XOR logic gate)
- Scaling to 200+ nodes with MNIST classifier
- How all components work together
- Best practices and troubleshooting

Perfect for both beginners and advanced users!

## 📖 Additional Documentation

- **[System Integration Guide](docs/system_integration_guide.md)** - Deep dive into how firmware, tools, and protocols integrate
- **[API Reference](docs/api_reference.md)** - Complete REST API documentation
- **[User Guide](docs/user_guide.md)** - Comprehensive command reference
- **[Multi-Backplane Guide](docs/multi_backplane_guide.md)** - Scaling to hundreds of nodes
- **[Firmware Development Guide](docs/firmware_development_guide.md)** - Creating and flashing custom firmware

