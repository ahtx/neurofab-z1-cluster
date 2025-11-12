# Multi-Backplane Quick Start Guide

**NeuroFab Z1 Cluster - Managing 200+ Nodes**

## TL;DR

Yes, `nls` and all other tools now support multiple backplanes with 200+ nodes!

```bash
# Setup (one time)
nconfig init -o ~/.neurofab/cluster.json
nconfig add backplane-0 192.168.1.222 --nodes 16
nconfig add backplane-1 192.168.1.223 --nodes 16
# ... add up to 13+ backplanes for 200+ nodes

# Use it
nls --all --parallel              # List all 200+ nodes (fast!)
nstat --all                       # Status of entire cluster
```

## How It Works

### Single Backplane (Original)

```
[nls] --HTTP--> [Controller 192.168.1.222] --Z1 Bus--> [16 Nodes]
```

**Usage:**
```bash
nls -c 192.168.1.222              # Explicit IP
nls                               # Uses default 192.168.1.222
```

### Multi-Backplane (New!)

```
                    ┌─> [Controller 192.168.1.222] --Z1 Bus--> [16 Nodes] bp-0
                    │
[nls --all] ───────┼─> [Controller 192.168.1.223] --Z1 Bus--> [16 Nodes] bp-1
                    │
                    ├─> [Controller 192.168.1.224] --Z1 Bus--> [16 Nodes] bp-2
                    │
                    └─> ... up to 13+ backplanes (200+ nodes)
```

**Usage:**
```bash
nls --all                         # Query all backplanes
nls --all --parallel              # Query in parallel (faster!)
nls --backplane backplane-0       # Query specific backplane
```

## Setup for 200 Nodes (13 Backplanes)

### Step 1: Create Configuration

```bash
# Initialize configuration file
nconfig init -o ~/.neurofab/cluster.json

# Add all 13 backplanes
nconfig add backplane-0  192.168.1.222 --nodes 16 --description "Rack 1, Slot 1"
nconfig add backplane-1  192.168.1.223 --nodes 16 --description "Rack 1, Slot 2"
nconfig add backplane-2  192.168.1.224 --nodes 16 --description "Rack 1, Slot 3"
nconfig add backplane-3  192.168.1.225 --nodes 16 --description "Rack 1, Slot 4"
nconfig add backplane-4  192.168.1.226 --nodes 16 --description "Rack 2, Slot 1"
nconfig add backplane-5  192.168.1.227 --nodes 16 --description "Rack 2, Slot 2"
nconfig add backplane-6  192.168.1.228 --nodes 16 --description "Rack 2, Slot 3"
nconfig add backplane-7  192.168.1.229 --nodes 16 --description "Rack 2, Slot 4"
nconfig add backplane-8  192.168.1.230 --nodes 16 --description "Rack 3, Slot 1"
nconfig add backplane-9  192.168.1.231 --nodes 16 --description "Rack 3, Slot 2"
nconfig add backplane-10 192.168.1.232 --nodes 16 --description "Rack 3, Slot 3"
nconfig add backplane-11 192.168.1.233 --nodes 16 --description "Rack 3, Slot 4"
nconfig add backplane-12 192.168.1.234 --nodes 16 --description "Rack 4, Slot 1"

# Verify configuration
nconfig list
```

**Expected Output:**
```
Cluster Configuration: /home/user/.neurofab/cluster.json
================================================================================
NAME             CONTROLLER IP    NODES  DESCRIPTION
--------------------------------------------------------------------------------
backplane-0      192.168.1.222    16     Rack 1, Slot 1
backplane-1      192.168.1.223    16     Rack 1, Slot 2
...
backplane-12     192.168.1.234    16     Rack 4, Slot 1

Total: 13 backplane(s), 208 nodes
```

### Step 2: List All Nodes

```bash
# List all 208 nodes
nls --all --parallel
```

**Expected Output:**
```
BACKPLANE        NODE  STATUS    MEMORY      UPTIME
----------------------------------------------------------------------
backplane-0         0  active    7.85 MB     2h 15m
backplane-0         1  active    7.85 MB     2h 15m
...
backplane-12       15  active    7.85 MB     2h 15m

Total: 208 nodes across 13 backplane(s)
```

### Step 3: Use Other Tools

All tools work with multi-backplane configurations:

```bash
# Cluster status
nstat --all

# Ping all nodes
nping --all all

# Deploy SNN across all backplanes
nsnn deploy large_network.json
```

## Performance

### Parallel vs Sequential Queries

**Sequential (default):**
- Queries each controller one at a time
- Total time = 13 × 50ms = 650ms

**Parallel (recommended):**
- Queries all controllers simultaneously
- Total time ≈ 70ms (13× faster!)

```bash
# Always use --parallel for multi-backplane
nls --all --parallel
```

## Node Addressing

In multi-backplane mode, nodes are addressed as:

```
<backplane_name>:<node_id>
```

**Examples:**
- `backplane-0:5` = Node 5 on backplane-0
- `backplane-12:15` = Node 15 on backplane-12

## Configuration File Locations

The tools search for configuration in this order:

1. `--config <file>` (command-line option)
2. `~/.neurofab/cluster.json` (user config)
3. `/etc/neurofab/cluster.json` (system config)
4. `./cluster.json` (current directory)

If no config found, defaults to single backplane at `192.168.1.222`.

## Example: Automated Setup Script

```bash
#!/bin/bash
# setup_cluster.sh - Configure 13-backplane cluster

# Create config
nconfig init -o ~/.neurofab/cluster.json

# Add all backplanes
for i in {0..12}; do
    ip="192.168.1.$((222 + i))"
    rack=$((i / 4 + 1))
    slot=$((i % 4 + 1))
    nconfig add "backplane-$i" "$ip" --nodes 16 \
        --description "Rack $rack, Slot $slot"
done

# Verify
echo "Configuration complete!"
nconfig list

# Test connectivity
echo -e "\nTesting connectivity..."
nls --all --parallel
```

## Backward Compatibility

All original single-backplane commands still work:

```bash
# Old way (still works)
nls -c 192.168.1.222

# New way (with config)
nls --all
```

## Common Commands

```bash
# Configuration management
nconfig init                      # Create new config
nconfig list                      # List all backplanes
nconfig add <name> <ip>          # Add backplane
nconfig remove <name>            # Remove backplane
nconfig show <name>              # Show details

# Cluster operations
nls --all --parallel             # List all nodes (fast)
nls --backplane bp-0             # List specific backplane
nstat --all                      # Cluster status
nping --all all                  # Ping all nodes

# Legacy single-backplane mode
nls -c 192.168.1.222             # Direct IP (bypasses config)
```

## FAQ

**Q: Can I mix backplanes with different node counts?**

A: Yes! Some backplanes can have fewer than 16 nodes:

```bash
nconfig add dev-backplane 192.168.1.240 --nodes 4
```

**Q: How do I query just one backplane?**

A: Use `--backplane`:

```bash
nls --backplane backplane-0
```

Or use legacy mode:

```bash
nls -c 192.168.1.222
```

**Q: Can I have multiple configuration files?**

A: Yes! Use `--config`:

```bash
nls --config production.json --all
nls --config development.json --all
```

**Q: What's the maximum number of backplanes?**

A: No hard limit. Tested with 13 backplanes (208 nodes). Should work with 20+ backplanes (300+ nodes).

**Q: Do I need to update my existing scripts?**

A: No! Single-backplane mode (`-c` flag) still works exactly as before.

## Network Requirements

For 200+ nodes:

- **Switch**: 24-port Gigabit (minimum), 48-port recommended
- **Bandwidth**: 1 Gbps per controller
- **Latency**: <1ms between controllers (same subnet recommended)
- **IP Range**: Sequential IPs recommended (e.g., 192.168.1.222-234)

## Troubleshooting

**Problem:** `nls --all` shows "No backplanes configured"

**Solution:** Create a configuration file:
```bash
nconfig init -o ~/.neurofab/cluster.json
nconfig add backplane-0 192.168.1.222 --nodes 16
```

---

**Problem:** Some backplanes show errors

**Solution:** Check specific backplane:
```bash
nls --backplane backplane-0
ping 192.168.1.222
```

---

**Problem:** Queries are slow

**Solution:** Use `--parallel` flag:
```bash
nls --all --parallel
```

## More Information

- Full documentation: `docs/multi_backplane_guide.md`
- User guide: `docs/user_guide.md`
- API reference: `docs/api_reference.md`

---

**Summary:** Yes, `nls` and all tools now support 200+ nodes across multiple backplanes. Create a config with `nconfig`, then use `--all --parallel` for fast cluster-wide operations!
