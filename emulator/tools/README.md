# Z1 Emulator Tools

This directory contains the same cluster management tools as `python_tools/bin/`, but configured to work with the Z1 emulator.

## Automatic Emulator Detection

The tools automatically detect when they're communicating with the emulator by checking the `/api/emulator/status` endpoint. No modifications to the tools are needed - they work with both real hardware and the emulator.

## Usage

### 1. Start the Emulator

```bash
# From the emulator directory
cd /path/to/neurofab_system/emulator
python3 z1_emulator.py
```

The emulator will start on `http://127.0.0.1:8000` by default.

### 2. Use the Tools

```bash
# Add tools to PATH
export PATH=$PATH:/path/to/neurofab_system/emulator/tools

# List nodes (emulator defaults to 127.0.0.1:8000)
nls -c 127.0.0.1

# Or set environment variable
export Z1_CONTROLLER_IP=127.0.0.1

# Now tools will use emulator by default
nls
nping all
nstat
```

## Tools Available

All standard Z1 tools work with the emulator:

- **nls** - List nodes
- **nping** - Ping nodes
- **nstat** - Cluster status
- **ncp** - Copy data to nodes
- **ncat** - Display node data
- **nconfig** - Cluster configuration
- **nsnn** - SNN management
- **nflash** - Firmware flashing

## Emulator-Specific Features

When connected to the emulator, you get additional capabilities:

### Check Emulator Status

```bash
curl http://127.0.0.1:8000/api/emulator/status
```

Response:
```json
{
  "emulator": true,
  "version": "1.0.0",
  "cluster_info": {
    "total_backplanes": 1,
    "total_nodes": 16,
    "active_nodes": 16,
    "simulation_running": true
  }
}
```

### Reset Emulator

```bash
curl -X POST http://127.0.0.1:8000/api/emulator/reset
```

### Get/Set Configuration

```bash
# Get configuration
curl http://127.0.0.1:8000/api/emulator/config

# Update simulation parameters
curl -X POST http://127.0.0.1:8000/api/emulator/config \
  -H "Content-Type: application/json" \
  -d '{"simulation": {"timestep_us": 500}}'
```

## Example Workflow

```bash
# 1. Start emulator
python3 ../z1_emulator.py &

# 2. List nodes
nls -c 127.0.0.1

# 3. Deploy SNN
nsnn deploy ../examples/xor_snn.json -c 127.0.0.1

# 4. Start SNN
nsnn start -c 127.0.0.1

# 5. Monitor activity
nsnn monitor 5000 -c 127.0.0.1

# 6. Stop emulator
curl -X POST http://127.0.0.1:8000/api/emulator/reset
```

## Differences from Real Hardware

### Advantages
- ✅ No physical hardware required
- ✅ Instant reset and reconfiguration
- ✅ Perfect observability (all internal state visible)
- ✅ No risk of hardware damage
- ✅ Faster than real-time simulation possible

### Limitations
- ⚠️ Simplified timing model (not cycle-accurate)
- ⚠️ No actual GPIO/LED hardware
- ⚠️ No power consumption modeling
- ⚠️ Python overhead (slower than real hardware for large networks)

## Tips

1. **Use localhost**: The emulator runs on localhost, so use `127.0.0.1` or `localhost` as the controller IP

2. **Multiple backplanes**: Edit the emulator config to simulate multi-backplane clusters

3. **Fast testing**: The emulator is perfect for rapid prototyping and testing before deploying to real hardware

4. **Debugging**: Use the emulator's observability features to debug SNN algorithms

5. **CI/CD**: Run automated tests against the emulator in your CI pipeline
