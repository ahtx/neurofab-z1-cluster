# Multi-Backplane Cluster Management Guide

**Author:** NeuroFab  
**Date:** November 11, 2025

## Overview

The NeuroFab Z1 cluster management tools support multi-backplane configurations, allowing you to manage hundreds of nodes across multiple controller boards from a single interface. This guide explains how to configure and use the tools with multi-backplane setups.

## Architecture

### Single Backplane (16 nodes)

```
[Workstation] --HTTP--> [Controller 192.168.1.222] --Z1 Bus--> [16 Compute Nodes]
```

### Multi-Backplane (96 nodes example)

```
                                    ┌─> [Controller 192.168.1.222] --Z1 Bus--> [16 Nodes] Backplane-0
                                    │
[Workstation] --HTTP--> [Network]──┼─> [Controller 192.168.1.223] --Z1 Bus--> [16 Nodes] Backplane-1
                                    │
                                    ├─> [Controller 192.168.1.224] --Z1 Bus--> [16 Nodes] Backplane-2
                                    │
                                    ├─> [Controller 192.168.1.225] --Z1 Bus--> [16 Nodes] Backplane-3
                                    │
                                    ├─> [Controller 192.168.1.226] --Z1 Bus--> [16 Nodes] Backplane-4
                                    │
                                    └─> [Controller 192.168.1.227] --Z1 Bus--> [16 Nodes] Backplane-5
```

Each backplane has its own controller with a unique IP address. The Python tools can query all controllers in parallel to provide a unified view of the entire cluster.

## Configuration

### Creating a Cluster Configuration

Use the `nconfig` utility to create and manage cluster configurations:

```bash
# Create a new configuration file
nconfig init -o ~/.neurofab/cluster.json

# Add backplanes
nconfig add backplane-0 192.168.1.222 --nodes 16 --description "Rack 1, Slot 1"
nconfig add backplane-1 192.168.1.223 --nodes 16 --description "Rack 1, Slot 2"
nconfig add backplane-2 192.168.1.224 --nodes 16 --description "Rack 1, Slot 3"

# List configured backplanes
nconfig list
```

### Configuration File Format

The cluster configuration is stored in JSON format:

```json
{
  "backplanes": [
    {
      "name": "backplane-0",
      "controller_ip": "192.168.1.222",
      "controller_port": 80,
      "node_count": 16,
      "description": "Rack 1, Slot 1 - Primary backplane"
    },
    {
      "name": "backplane-1",
      "controller_ip": "192.168.1.223",
      "controller_port": 80,
      "node_count": 16,
      "description": "Rack 1, Slot 2"
    }
  ]
}
```

### Configuration File Locations

The tools search for configuration files in the following order:

1. File specified with `--config` option
2. `~/.neurofab/cluster.json` (user configuration)
3. `/etc/neurofab/cluster.json` (system configuration)
4. `./cluster.json` (current directory)

If no configuration file is found, tools default to single-backplane mode with controller at `192.168.1.222`.

## Using Multi-Backplane Tools

### Listing All Nodes

The enhanced `nls` command can list nodes from all configured backplanes:

```bash
# List all nodes from all backplanes
nls --all

# List with verbose output
nls --all -v

# List from specific backplane
nls --backplane backplane-0

# Use parallel queries for faster response
nls --all --parallel
```

**Example Output (96 nodes across 6 backplanes):**

```
BACKPLANE        NODE  STATUS    MEMORY      UPTIME
----------------------------------------------------------------------
backplane-0         0  active    7.85 MB     2h 15m
backplane-0         1  active    7.85 MB     2h 15m
...
backplane-0        15  active    7.85 MB     2h 15m
backplane-1         0  active    7.85 MB     2h 15m
backplane-1         1  active    7.85 MB     2h 15m
...
backplane-5        15  active    7.85 MB     2h 15m

Total: 96 nodes across 6 backplane(s)
```

### Node Addressing

In multi-backplane configurations, nodes are addressed using the format:

```
<backplane_name>:<node_id>
```

For example:
- `backplane-0:5` - Node 5 on backplane-0
- `backplane-2:12` - Node 12 on backplane-2

### Legacy Single-Backplane Mode

All tools still support the original single-backplane mode using the `-c` option:

```bash
# Query specific controller directly
nls -c 192.168.1.222

# This bypasses the configuration file
nping -c 192.168.1.223 all
```

## Performance Considerations

### Parallel Queries

When querying multiple backplanes, use the `--parallel` flag to query all controllers simultaneously:

```bash
# Sequential queries (slower)
nls --all

# Parallel queries (much faster)
nls --all --parallel
```

**Performance Comparison:**

| Backplanes | Sequential | Parallel | Speedup |
|------------|-----------|----------|---------|
| 1          | 50ms      | 50ms     | 1×      |
| 6          | 300ms     | 60ms     | 5×      |
| 12         | 600ms     | 70ms     | 8.6×    |

### Network Topology

For best performance with large clusters:

1. **Use Gigabit Ethernet**: Ensure all controllers are on a Gigabit network
2. **Minimize Latency**: Place controllers on the same switch/subnet
3. **Use Parallel Queries**: Enable `--parallel` for multi-backplane operations
4. **Consider Load**: Distribute queries across time for very large clusters (200+ nodes)

## Scaling to 200+ Nodes

### Example: 13 Backplanes (208 nodes)

```bash
# Initialize configuration
nconfig init -o ~/.neurofab/cluster.json

# Add 13 backplanes
for i in {0..12}; do
    ip="192.168.1.$((222 + i))"
    nconfig add "backplane-$i" "$ip" --nodes 16 --description "Rack $((i/4 + 1)), Slot $((i%4 + 1))"
done

# Verify configuration
nconfig list

# List all 208 nodes
nls --all --parallel
```

**Expected Output:**

```
Total: 208 nodes across 13 backplane(s)
```

### Network Configuration

For large clusters, ensure your network can handle the traffic:

**IP Address Planning:**

```
192.168.1.222 - backplane-0  (Rack 1, Slot 1)
192.168.1.223 - backplane-1  (Rack 1, Slot 2)
192.168.1.224 - backplane-2  (Rack 1, Slot 3)
192.168.1.225 - backplane-3  (Rack 1, Slot 4)
192.168.1.226 - backplane-4  (Rack 2, Slot 1)
...
192.168.1.234 - backplane-12 (Rack 4, Slot 1)
```

**Switch Requirements:**

- Minimum: 24-port Gigabit switch
- Recommended: 48-port Gigabit managed switch with VLAN support
- For 200+ nodes: Multiple switches with 10GbE uplinks

## Advanced Configuration

### Multiple Racks

Organize backplanes by physical location:

```json
{
  "backplanes": [
    {
      "name": "rack1-slot1",
      "controller_ip": "192.168.1.222",
      "node_count": 16,
      "description": "Rack 1, Slot 1"
    },
    {
      "name": "rack1-slot2",
      "controller_ip": "192.168.1.223",
      "node_count": 16,
      "description": "Rack 1, Slot 2"
    },
    {
      "name": "rack2-slot1",
      "controller_ip": "192.168.2.222",
      "node_count": 16,
      "description": "Rack 2, Slot 1"
    }
  ]
}
```

### Custom Node Counts

If some backplanes have fewer than 16 nodes:

```bash
nconfig add backplane-dev 192.168.1.230 --nodes 4 --description "Development backplane"
```

### Multiple Configurations

Maintain separate configurations for different environments:

```bash
# Production cluster
nls --config /etc/neurofab/production.json --all

# Development cluster
nls --config ~/.neurofab/dev.json --all

# Test cluster
nls --config ./test_cluster.json --all
```

## Troubleshooting

### Backplane Not Responding

If a backplane doesn't respond:

```bash
# Check specific backplane
nls --backplane backplane-0

# Ping the controller
ping 192.168.1.222

# Check if HTTP server is running
curl http://192.168.1.222/api/nodes
```

### Configuration Issues

```bash
# Verify configuration is valid
nconfig list --json | python3 -m json.tool

# Show details of specific backplane
nconfig show backplane-0

# Remove and re-add problematic backplane
nconfig remove backplane-0
nconfig add backplane-0 192.168.1.222 --nodes 16
```

### Performance Issues

If queries are slow:

1. Use `--parallel` flag
2. Check network connectivity: `ping <controller_ip>`
3. Verify no packet loss: `ping -c 100 <controller_ip>`
4. Check switch performance
5. Consider querying backplanes in batches

## Best Practices

### Naming Conventions

Use consistent naming for backplanes:

```
backplane-0, backplane-1, backplane-2  (simple numbering)
rack1-slot1, rack1-slot2, rack2-slot1  (physical location)
prod-bp-0, prod-bp-1, dev-bp-0         (environment-based)
```

### IP Address Assignment

Assign sequential IP addresses for easier management:

```
192.168.1.222 - First backplane
192.168.1.223 - Second backplane
192.168.1.224 - Third backplane
...
```

### Documentation

Document your cluster configuration:

```bash
# Add descriptions to all backplanes
nconfig add backplane-0 192.168.1.222 \
    --description "Rack 1, Slot 1 - Primary, installed 2025-11-01"
```

### Monitoring

Regularly check cluster health:

```bash
# Daily health check
nls --all --parallel > cluster_status_$(date +%Y%m%d).txt

# Check for offline nodes
nls --all --json | jq '.backplanes[] | select(.error)'
```

## Example Workflows

### Adding a New Backplane

```bash
# 1. Configure network (assign IP to controller)
# 2. Add to configuration
nconfig add backplane-6 192.168.1.228 --nodes 16 --description "Rack 2, Slot 3"

# 3. Verify connectivity
nls --backplane backplane-6

# 4. Test all nodes
nping --backplane backplane-6 all
```

### Removing a Backplane

```bash
# 1. Stop any running SNNs using the backplane
nsnn stop

# 2. Remove from configuration
nconfig remove backplane-6

# 3. Verify
nconfig list
```

### Cluster-Wide Status Check

```bash
# Get status of all nodes
nls --all --parallel -v > cluster_report.txt

# Get JSON for processing
nls --all --parallel --json > cluster_status.json

# Count active nodes
nls --all --json | jq '.total_nodes'
```

## Conclusion

The multi-backplane support in NeuroFab Z1 cluster tools enables seamless management of large-scale neuromorphic computing systems with 200+ nodes. By using configuration files and parallel queries, you can efficiently manage and monitor your entire cluster from a single workstation.

For additional information, see the main User Guide and API Reference documentation.
