# Z1 Cluster Emulator

A complete software emulation of the NeuroFab Z1 neuromorphic computing cluster. Develop, test, and demonstrate SNN applications without requiring physical hardware.

## Features

- ✅ **Full Hardware Simulation** - Emulates RP2350B nodes with 2MB Flash + 8MB PSRAM
- ✅ **Multi-Backplane Support** - Simulate clusters with 200+ nodes
- ✅ **SNN Execution** - Runs Leaky Integrate-and-Fire (LIF) neuron model
- ✅ **Bus Communication** - Simulates Z1 matrix bus with realistic latency
- ✅ **HTTP REST API** - 100% compatible with real hardware API
- ✅ **Firmware Management** - Simulates bootloader and firmware flashing
- ✅ **Tool Compatibility** - Works with all existing Z1 tools (nls, nsnn, nflash, etc.)
- ✅ **Real-time or Fast Mode** - Run at wall-clock speed or as fast as possible

## Quick Start

### Installation

```bash
# Install dependencies
pip3 install flask numpy requests

# Navigate to emulator directory
cd /path/to/neurofab_system/emulator
```

### Run the Emulator

```bash
# Start with default configuration (16 nodes on localhost:8000)
python3 z1_emulator.py

# Or with custom configuration
python3 z1_emulator.py -c my_config.json

# Create a configuration template
python3 z1_emulator.py --create-config my_config.json
```

### Use the Tools

```bash
# In another terminal, use the tools
export Z1_CONTROLLER_IP=127.0.0.1

# List nodes
./tools/nls

# Deploy and run an SNN
./tools/nsnn deploy examples/xor_snn.json
./tools/nsnn start
./tools/nsnn monitor 5000
```

## Directory Structure

```
emulator/
├── z1_emulator.py          # Main emulator launcher
├── core/                   # Core emulation engine
│   ├── node.py            # Compute node simulation
│   ├── cluster.py         # Backplane and cluster management
│   ├── snn_engine.py      # SNN execution engine
│   └── api_server.py      # HTTP REST API server
├── tools/                  # Z1 tools (work with emulator)
│   ├── nls, nping, nstat  # Node management
│   ├── ncp, ncat          # Memory operations
│   ├── nsnn               # SNN deployment
│   └── nflash             # Firmware flashing
├── lib/                    # Python libraries
│   ├── z1_client.py       # API client
│   ├── cluster_config.py  # Configuration management
│   └── snn_compiler.py    # SNN topology compiler
├── examples/               # Example configurations and SNNs
└── tests/                  # Test scripts

## Configuration

Create a configuration file to customize the emulator:

```json
{
  "backplanes": [
    {
      "name": "backplane-0",
      "controller_ip": "127.0.0.1",
      "controller_port": 8000,
      "node_count": 16,
      "description": "Primary backplane"
    },
    {
      "name": "backplane-1",
      "controller_ip": "127.0.0.1",
      "controller_port": 8001,
      "node_count": 16,
      "description": "Secondary backplane"
    }
  ],
  "simulation": {
    "mode": "real-time",
    "timestep_us": 1000,
    "bus_latency_us": 2.0
  },
  "memory": {
    "flash_size_mb": 2,
    "psram_size_mb": 8
  },
  "server": {
    "host": "127.0.0.1",
    "port": 8000
  }
}
```

## API Endpoints

The emulator implements all standard Z1 API endpoints plus emulator-specific ones:

### Standard Endpoints (Hardware Compatible)

- `GET /api/nodes` - List all nodes
- `GET /api/nodes/{id}` - Get node info
- `POST /api/nodes/{id}/reset` - Reset node
- `GET /api/nodes/{id}/memory` - Read memory
- `POST /api/nodes/{id}/memory` - Write memory
- `GET /api/nodes/{id}/firmware` - Get firmware info
- `POST /api/nodes/{id}/firmware` - Flash firmware
- `POST /api/snn/start` - Start SNN execution
- `POST /api/snn/stop` - Stop SNN execution
- `GET /api/snn/activity` - Get spike activity
- `POST /api/snn/input` - Inject input spikes

### Emulator-Specific Endpoints

- `GET /api/emulator/status` - Get emulator status (identifies as emulator)
- `POST /api/emulator/reset` - Reset entire emulator
- `GET /api/emulator/config` - Get configuration
- `POST /api/emulator/config` - Update configuration

## Example: Running XOR SNN

```bash
# 1. Start emulator
python3 z1_emulator.py &

# 2. Deploy XOR network
./tools/nsnn deploy examples/xor_snn.json -c 127.0.0.1

# 3. Start execution
./tools/nsnn start -c 127.0.0.1

# 4. Test with input patterns
./tools/nsnn inject examples/xor_input_00.json -c 127.0.0.1
./tools/nsnn monitor 1000 -c 127.0.0.1

./tools/nsnn inject examples/xor_input_01.json -c 127.0.0.1
./tools/nsnn monitor 1000 -c 127.0.0.1

# 5. Stop execution
./tools/nsnn stop -c 127.0.0.1
```

## Example: Multi-Backplane Cluster

```bash
# Create multi-backplane config
cat > multi_backplane.json << 'EOF'
{
  "backplanes": [
    {"name": "bp-0", "controller_ip": "127.0.0.1", "controller_port": 8000, "node_count": 16},
    {"name": "bp-1", "controller_ip": "127.0.0.1", "controller_port": 8001, "node_count": 16},
    {"name": "bp-2", "controller_ip": "127.0.0.1", "controller_port": 8002, "node_count": 16}
  ],
  "simulation": {"timestep_us": 1000, "bus_latency_us": 2.0}
}
EOF

# Start emulator with 48 nodes
python3 z1_emulator.py -c multi_backplane.json

# Use tools with multi-backplane cluster
./tools/nls --all  # List all 48 nodes
```

## Use Cases

### 1. Development
- Develop SNN algorithms without hardware
- Test cluster management scripts
- Prototype firmware applications

### 2. Testing
- Automated integration tests in CI/CD
- Regression testing
- Performance benchmarking

### 3. Education
- Learn neuromorphic computing concepts
- Visualize spike propagation
- Experiment with network topologies

### 4. Demonstration
- Show system capabilities without hardware
- Interactive demos
- Record and replay experiments

## Performance

### Real Hardware vs Emulator

| Metric | Real Hardware | Emulator |
|--------|--------------|----------|
| Bus Latency | 1-5 μs | Configurable (simulated) |
| Memory Access | ~100 ns | Instant (Python dict) |
| Spike Propagation | ~10 μs | ~100 μs (Python overhead) |
| Max Neurons/Node | 4,096 | Limited by RAM |
| Network Latency | ~1 ms (Ethernet) | ~1 ms (localhost) |

### Scaling

- **Small networks** (< 1000 neurons): Real-time or faster
- **Medium networks** (1000-10000 neurons): Near real-time
- **Large networks** (> 10000 neurons): Slower than real-time

## Limitations

The emulator is not cycle-accurate and has some simplifications:

- ⚠️ **Timing**: Simplified timing model, not cycle-accurate
- ⚠️ **Hardware**: No actual GPIO, LED, or peripheral simulation
- ⚠️ **Power**: No power consumption modeling
- ⚠️ **Performance**: Python overhead makes it slower for very large networks

## Advanced Features

### Custom Simulation Speed

```python
# Modify simulation timestep
curl -X POST http://127.0.0.1:8000/api/emulator/config \
  -H "Content-Type: application/json" \
  -d '{"simulation": {"timestep_us": 100}}'  # 10x faster
```

### Observability

```python
# Get detailed node state
curl http://127.0.0.1:8000/api/nodes/0

# Get global activity
curl http://127.0.0.1:8000/api/snn/activity
```

### Programmatic Control

```python
import requests

# Start emulator programmatically
# (Run z1_emulator.py in background first)

# Check if emulator is running
r = requests.get('http://127.0.0.1:8000/api/emulator/status')
if r.json().get('emulator'):
    print("Emulator detected!")

# Deploy SNN
with open('my_network.json') as f:
    topology = json.load(f)
requests.post('http://127.0.0.1:8000/api/snn/deploy', json=topology)

# Start execution
requests.post('http://127.0.0.1:8000/api/snn/start')
```

## Troubleshooting

### Port Already in Use

```bash
# Use a different port
python3 z1_emulator.py --port 8080
```

### Tools Can't Connect

```bash
# Make sure emulator is running
curl http://127.0.0.1:8000/api/emulator/status

# Check controller IP in tools
./tools/nls -c 127.0.0.1
```

### Slow Simulation

```bash
# Reduce timestep for faster simulation (less accurate)
python3 z1_emulator.py  # Edit config to set timestep_us to smaller value
```

## Contributing

The emulator is designed to be extended. Key extension points:

- **New neuron models**: Modify `snn_engine.py`
- **Hardware peripherals**: Add to `node.py`
- **Visualization**: Create web dashboard using `/api/*` endpoints
- **Performance**: Add GPU acceleration for large networks

## See Also

- [Architecture Documentation](ARCHITECTURE.md) - Detailed design
- [Tools README](tools/README.md) - Tool usage with emulator
- [Examples](examples/) - Example configurations and SNNs
- [Main Project README](../README.md) - Overall Z1 system documentation

## License

Copyright © 2025 NeuroFab Corp. All Rights Reserved.
