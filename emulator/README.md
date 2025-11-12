# NeuroFab Z1 Emulator

The Z1 emulator provides a complete software simulation of the NeuroFab Z1 neuromorphic cluster, allowing you to develop and test SNNs without physical hardware.

## Features

- Full hardware simulation (2MB Flash + 8MB PSRAM per node)
- Multi-backplane support (up to 200+ nodes)
- SNN execution with LIF neurons
- STDP learning implementation
- HTTP REST API (100% compatible with hardware)
- Real-time and fast-forward modes

## Quick Start

```bash
# Start emulator
cd emulator
python3 z1_emulator.py

# In another terminal, use tools
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000
nls
```

## Configuration

Create a configuration file (`config.json`):

```json
{
  "backplanes": [
    {
      "name": "backplane-0",
      "nodes": 16,
      "port": 8000
    }
  ]
}
```

Then start with:

```bash
python3 z1_emulator.py -c config.json
```

## Testing

```bash
# Deploy and test XOR network
./tools/nsnn deploy examples/xor_snn.json
./tools/nsnn start
./tools/nsnn inject examples/xor_input_01.json
./tools/nsnn monitor 1000
```

## Limitations

- Simplified timing model (not cycle-accurate)
- Python overhead (slower than hardware for large networks)
- No GPIO/LED/peripheral simulation

For more details, see the [User Guide](../docs/USER_GUIDE.md).
