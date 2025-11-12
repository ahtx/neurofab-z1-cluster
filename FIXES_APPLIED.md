# Fixes Applied to NeuroFab Z1 System

This document details all fixes applied to address issues with hardcoded IPs/ports, import errors, and API compatibility.

## Issues Fixed

### 1. Python Import Errors ✅

**Problem:** Relative imports in emulator core modules failed when running the emulator.

**Files Fixed:**
- `emulator/core/cluster.py`
- `emulator/core/api_server.py`

**Changes:**
```python
# Before
from .node import ComputeNode, NodeStatus
from .cluster import Cluster
from .snn_engine import ClusterSNNCoordinator, Spike

# After
from node import ComputeNode, NodeStatus
from cluster import Cluster
from snn_engine import ClusterSNNCoordinator, Spike
```

**Reason:** The emulator launcher (`z1_emulator.py`) adds the `core/` directory to the Python path, so absolute imports within the core package work correctly.

---

### 2. API Field Compatibility Issues ✅

**Problem:** Tools expected different field names than what the emulator returned.

**File Fixed:** `emulator/core/node.py`

**Changes:**
```python
# Added compatibility fields to get_info() method
return {
    'id': self.node_id,              # Tools expect 'id'
    'node_id': self.node_id,         # Keep for compatibility
    'memory_free': free_mem,         # Tools expect 'memory_free'
    'free_memory': free_mem,         # Keep for compatibility
    # ... rest of fields
}
```

**Impact:** Tools now work seamlessly with both emulator and real hardware.

---

### 3. Hardcoded Production IP Addresses ✅

**Problem:** All tools defaulted to production IP `192.168.1.222` instead of being flexible.

**Solution:** Enhanced `cluster_config.py` with smart defaults and environment variable support.

**New Behavior:**
1. **Environment variables** (highest priority)
   - `Z1_CONTROLLER_IP` - Controller IP address
   - `Z1_CONTROLLER_PORT` - Controller port

2. **Configuration file**
   - `~/.neurofab/cluster.json`
   - `/etc/neurofab/cluster.json`
   - `./cluster.json`

3. **Auto-detection**
   - Checks for emulator at `127.0.0.1:8000`
   - Auto-detects port based on IP (localhost → 8000, others → 80)

4. **Fallback**
   - Defaults to `192.168.1.222:80` (real hardware)

---

### 4. Hardcoded Production Ports ✅

**Problem:** Tools hardcoded to port 80 even when using localhost.

**Solution:** Implemented smart port detection in `cluster_config.py`:

```python
def _auto_detect_port(self, ip: str) -> int:
    """Auto-detect port based on IP address."""
    if ip in ['127.0.0.1', 'localhost']:
        return 8000  # Emulator
    return 80  # Real hardware
```

**Impact:** Tools automatically use the correct port based on the controller IP.

---

### 5. Environment Variable Support ✅

**Problem:** Tools didn't properly use `Z1_CONTROLLER_IP` and `Z1_CONTROLLER_PORT` environment variables.

**Solution:** Added comprehensive environment variable support in `cluster_config.py`:

```python
def _load_from_environment(self):
    """Load configuration from environment variables."""
    controller_ip = os.environ.get('Z1_CONTROLLER_IP')
    controller_port_str = os.environ.get('Z1_CONTROLLER_PORT')
    
    if controller_ip:
        if controller_port_str:
            controller_port = int(controller_port_str)
        else:
            controller_port = self._auto_detect_port(controller_ip)
        
        self.backplanes.append(BackplaneConfig(...))
```

**Usage:**
```bash
# Set environment variables
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000

# Tools automatically use these values
nls
nsnn deploy examples/xor_snn.json
nstat
```

---

### 6. Emulator Auto-Detection ✅

**Problem:** No automatic way to detect if emulator is running.

**Solution:** Added emulator detection in `cluster_config.py`:

```python
def get_default_backplane(self) -> BackplaneConfig:
    # ... check env vars and config ...
    
    # Try to auto-detect emulator
    try:
        import requests
        response = requests.get('http://127.0.0.1:8000/api/emulator/status', timeout=0.5)
        if response.status_code == 200 and response.json().get('emulator'):
            return BackplaneConfig(
                name='emulator',
                controller_ip='127.0.0.1',
                controller_port=8000,
                ...
            )
    except:
        pass
    
    # Default to real hardware
    return BackplaneConfig(...)
```

**Impact:** Tools automatically detect and connect to emulator if it's running on localhost.

---

## New Features Added

### 1. Environment Setup Script

**File:** `emulator/set_emulator_env.sh`

**Usage:**
```bash
source emulator/set_emulator_env.sh
```

**What it does:**
- Sets `Z1_CONTROLLER_IP=127.0.0.1`
- Sets `Z1_CONTROLLER_PORT=8000`
- Adds emulator tools to PATH
- Displays configuration summary

---

### 2. Quick Start Script

**File:** `emulator/quick_start.sh`

**Usage:**
```bash
./emulator/quick_start.sh
```

**What it does:**
1. Checks if emulator is running (starts it if not)
2. Sets environment variables
3. Lists cluster nodes
4. Deploys XOR SNN
5. Starts SNN execution
6. Provides next steps and commands

---

## Configuration Priority

The system now uses this priority order for configuration:

```
1. Environment Variables (Z1_CONTROLLER_IP, Z1_CONTROLLER_PORT)
   ↓
2. Configuration File (~/.neurofab/cluster.json, etc.)
   ↓
3. Auto-detection (check for emulator at 127.0.0.1:8000)
   ↓
4. Default (192.168.1.222:80 for real hardware)
```

---

## Usage Examples

### Example 1: Using Environment Variables

```bash
# Set environment
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000

# Use tools without -c flag
nls
nstat
nsnn deploy examples/xor_snn.json
```

### Example 2: Using Configuration File

```bash
# Create config
cat > ~/.neurofab/cluster.json << 'EOF'
{
  "backplanes": [
    {
      "name": "emulator",
      "controller_ip": "127.0.0.1",
      "controller_port": 8000,
      "node_count": 16
    }
  ]
}
EOF

# Tools automatically use config
nls
nstat
```

### Example 3: Auto-Detection

```bash
# Start emulator
cd emulator
python3 z1_emulator.py &

# Tools automatically detect emulator
nls  # Connects to 127.0.0.1:8000 automatically
```

### Example 4: Explicit Controller

```bash
# Override everything with -c flag
nls -c 192.168.1.222
nsnn deploy examples/xor_snn.json -c 192.168.1.223
```

---

## Testing the Fixes

### Test 1: Import Errors Fixed

```bash
cd emulator
python3 z1_emulator.py
# Should start without import errors
```

### Test 2: API Compatibility

```bash
# Start emulator
python3 emulator/z1_emulator.py &

# Test tools
export Z1_CONTROLLER_IP=127.0.0.1
./emulator/tools/nls  # Should list nodes with correct fields
```

### Test 3: Environment Variables

```bash
# Set variables
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000

# Test without -c flag
./emulator/tools/nls  # Should connect to emulator
```

### Test 4: Auto-Detection

```bash
# Start emulator
python3 emulator/z1_emulator.py &

# Don't set any environment variables
./emulator/tools/nls  # Should auto-detect emulator
```

---

## Migration Guide

### For Existing Users

**Before (hardcoded IPs):**
```bash
nls -c 192.168.1.222
nsnn deploy topology.json -c 192.168.1.222
```

**After (environment variables):**
```bash
export Z1_CONTROLLER_IP=192.168.1.222
nls
nsnn deploy topology.json
```

**Or use configuration file:**
```bash
# Create ~/.neurofab/cluster.json with your hardware IPs
nconfig init
nconfig add backplane-0 192.168.1.222

# Tools automatically use config
nls
nsnn deploy topology.json
```

### For Emulator Users

**Recommended setup:**
```bash
# Option 1: Use environment script
source emulator/set_emulator_env.sh
nls

# Option 2: Use quick start
./emulator/quick_start.sh

# Option 3: Manual environment
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000
nls
```

---

## Summary of Changes

| Issue | Status | Impact |
|-------|--------|--------|
| Python import errors | ✅ Fixed | Emulator starts without errors |
| API field compatibility | ✅ Fixed | Tools work with emulator |
| Hardcoded IPs | ✅ Fixed | Flexible configuration |
| Hardcoded ports | ✅ Fixed | Auto-detection works |
| Environment variables | ✅ Added | Easy configuration |
| Auto-detection | ✅ Added | Zero-config emulator use |
| Setup scripts | ✅ Added | Quick start workflows |

---

## Files Modified

1. `emulator/core/cluster.py` - Fixed imports
2. `emulator/core/api_server.py` - Fixed imports
3. `emulator/core/node.py` - Added API compatibility fields
4. `python_tools/lib/cluster_config.py` - Added environment support and auto-detection
5. `emulator/lib/cluster_config.py` - Synced with python_tools version

## Files Added

1. `emulator/set_emulator_env.sh` - Environment setup script
2. `emulator/quick_start.sh` - Quick start demonstration
3. `FIXES_APPLIED.md` - This document

---

## Verification

All fixes have been applied and tested. The system now:

✅ Starts without import errors  
✅ Works with environment variables  
✅ Auto-detects emulator  
✅ Maintains backward compatibility  
✅ Provides flexible configuration options  
✅ Includes helper scripts for easy setup  

**Status:** Ready for production use with both emulator and real hardware.
