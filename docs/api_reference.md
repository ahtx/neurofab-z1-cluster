# Z1 Cluster HTTP REST API Reference

**Author:** NeuroFab  
**Date:** November 11, 2025

## Overview

The Z1 Cluster HTTP REST API provides programmatic access to all cluster management and SNN operations. The API is exposed by the controller node on port 80 and follows RESTful conventions.

## Base URL

```
http://<controller_ip>:80/api
```

Default controller IP: `192.168.1.222`

## Authentication

Currently, no authentication is required. Future versions may implement API key or token-based authentication.

## Response Format

All responses are in JSON format with the following structure:

**Success Response:**

```json
{
  "status": "ok",
  "data": { ... }
}
```

**Error Response:**

```json
{
  "error": "Error message",
  "code": 400
}
```

## HTTP Status Codes

- `200 OK`: Request successful
- `201 Created`: Resource created successfully
- `400 Bad Request`: Invalid request parameters
- `404 Not Found`: Resource not found
- `500 Internal Server Error`: Server error
- `501 Not Implemented`: Feature not yet implemented

## Endpoints

### Node Management

#### GET /api/nodes

List all nodes in the cluster.

**Response:**

```json
{
  "nodes": [
    {
      "id": 0,
      "status": "active",
      "memory_free": 8257536,
      "uptime_ms": 8100000,
      "led_state": {"r": 0, "g": 255, "b": 0}
    },
    ...
  ]
}
```

#### GET /api/nodes/{node_id}

Get detailed information about a specific node.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Response:**

```json
{
  "id": 0,
  "status": "active",
  "memory_free": 8257536,
  "uptime_ms": 8100000,
  "neuron_count": 150,
  "spike_count": 15847
}
```

**Errors:**

- `400`: Invalid node ID
- `404`: Node not found or not responding

#### POST /api/nodes/{node_id}/reset

Reset a specific node.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Request Body:**

```json
{}
```

**Response:**

```json
{
  "status": "ok",
  "node_id": 0
}
```

#### POST /api/nodes/{node_id}/ping

Ping a node to test connectivity.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Request Body:**

```json
{}
```

**Response:**

```json
{
  "status": "ok",
  "node_id": 0
}
```

**Errors:**

- `404`: Node not responding

#### POST /api/nodes/{node_id}/led

Set LED color on a node.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Request Body:**

```json
{
  "r": 255,
  "g": 0,
  "b": 0
}
```

**Response:**

```json
{
  "status": "ok"
}
```

#### POST /api/nodes/discover

Discover all active nodes in the cluster.

**Request Body:**

```json
{}
```

**Response:**

```json
{
  "active_nodes": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
}
```

### Memory Operations

#### GET /api/nodes/{node_id}/memory

Read memory from a node.

**Parameters:**

- `node_id` (path): Node ID (0-15)
- `addr` (query): Memory address (hex or decimal)
- `len` (query): Number of bytes to read (max 4096)

**Example:**

```
GET /api/nodes/0/memory?addr=0x20000000&len=256
```

**Response:**

```json
{
  "addr": 536870912,
  "length": 256,
  "data": "AAAAAAAAAAAAAAAAAAAAAA..."
}
```

Note: `data` is base64-encoded binary data.

**Errors:**

- `400`: Invalid parameters or length too large
- `500`: Failed to read memory

#### POST /api/nodes/{node_id}/memory

Write memory to a node.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Request Body:**

```json
{
  "addr": 536870912,
  "data": "AAAAAAAAAAAAAAAAAAAAAA..."
}
```

Note: `data` is base64-encoded binary data.

**Response:**

```json
{
  "bytes_written": 256
}
```

**Errors:**

- `400`: Invalid parameters
- `500`: Failed to write memory

#### POST /api/nodes/{node_id}/execute

Execute code on a node.

**Parameters:**

- `node_id` (path): Node ID (0-15)

**Request Body:**

```json
{
  "entry_point": 536936448,
  "params": [100, 200, 300]
}
```

**Response:**

```json
{
  "execution_id": 1
}
```

### SNN Management

#### POST /api/snn/deploy

Deploy SNN topology to cluster.

**Request Body:**

```json
{
  "network_name": "MNIST_SNN",
  "neuron_count": 1794,
  "layers": [
    {
      "layer_id": 0,
      "layer_type": "input",
      "neuron_count": 784,
      "neuron_ids": [0, 783],
      "threshold": 1.0
    },
    ...
  ],
  "connections": [
    {
      "source_layer": 0,
      "target_layer": 1,
      "connection_type": "fully_connected",
      "weight_init": "random_normal",
      "weight_mean": 0.5,
      "weight_stddev": 0.1
    },
    ...
  ],
  "node_assignment": {
    "strategy": "balanced",
    "nodes": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
  }
}
```

**Response:**

```json
{
  "status": "ok",
  "neurons_deployed": 1794,
  "nodes_used": 12
}
```

#### GET /api/snn/topology

Get current SNN topology.

**Response:**

```json
{
  "network_name": "MNIST_SNN",
  "neuron_count": 1794,
  "layers": [...],
  "connections": [...]
}
```

#### POST /api/snn/weights

Update synaptic weights.

**Request Body:**

```json
{
  "updates": [
    {
      "neuron_id": 100,
      "synapse_idx": 5,
      "weight": 200
    },
    ...
  ]
}
```

**Response:**

```json
{
  "weights_updated": 10
}
```

#### GET /api/snn/activity

Capture spike activity for specified duration.

**Parameters:**

- `duration_ms` (query): Capture duration in milliseconds (default: 1000)

**Example:**

```
GET /api/snn/activity?duration_ms=5000
```

**Response:**

```json
{
  "spikes": [
    {
      "neuron_id": 100,
      "timestamp_us": 1234567,
      "node_id": 0
    },
    ...
  ]
}
```

#### POST /api/snn/input

Inject input spikes into network.

**Request Body:**

```json
{
  "spikes": [
    {
      "neuron_id": 0,
      "value": 1.0
    },
    {
      "neuron_id": 1,
      "value": 0.8
    },
    ...
  ]
}
```

**Response:**

```json
{
  "spikes_injected": 100
}
```

#### POST /api/snn/start

Start SNN execution on all nodes.

**Request Body:**

```json
{}
```

**Response:**

```json
{
  "status": "ok"
}
```

**Errors:**

- `400`: No SNN deployed
- `500`: Failed to start SNN

#### POST /api/snn/stop

Stop SNN execution on all nodes.

**Request Body:**

```json
{}
```

**Response:**

```json
{
  "status": "ok"
}
```

#### GET /api/snn/status

Get SNN execution status.

**Response:**

```json
{
  "state": "running",
  "network_name": "MNIST_SNN",
  "neuron_count": 1794,
  "active_neurons": 342,
  "total_spikes": 15847,
  "spike_rate_hz": 3169.4,
  "nodes_used": 12
}
```

## Rate Limiting

Currently, no rate limiting is implemented. Future versions may implement per-client rate limits.

## CORS

Cross-Origin Resource Sharing (CORS) is enabled with `Access-Control-Allow-Origin: *` to allow browser-based clients.

## Examples

### Python

```python
import requests
import json

# List nodes
response = requests.get('http://192.168.1.222/api/nodes')
nodes = response.json()['nodes']
print(f"Found {len(nodes)} nodes")

# Deploy SNN
with open('network.json', 'r') as f:
    topology = json.load(f)

response = requests.post('http://192.168.1.222/api/snn/deploy',
                        json=topology)
result = response.json()
print(f"Deployed {result['neurons_deployed']} neurons")

# Start SNN
response = requests.post('http://192.168.1.222/api/snn/start',
                        json={})
print(response.json())
```

### curl

```bash
# List nodes
curl http://192.168.1.222/api/nodes

# Ping node 0
curl -X POST http://192.168.1.222/api/nodes/0/ping

# Get SNN status
curl http://192.168.1.222/api/snn/status

# Start SNN
curl -X POST http://192.168.1.222/api/snn/start \
  -H "Content-Type: application/json" \
  -d '{}'
```

### JavaScript

```javascript
// List nodes
fetch('http://192.168.1.222/api/nodes')
  .then(response => response.json())
  .then(data => {
    console.log(`Found ${data.nodes.length} nodes`);
  });

// Deploy SNN
fetch('http://192.168.1.222/api/snn/deploy', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify(topology)
})
  .then(response => response.json())
  .then(data => {
    console.log(`Deployed ${data.neurons_deployed} neurons`);
  });
```

## Versioning

The current API version is v1. Future versions will be accessible via `/api/v2/` endpoints to maintain backward compatibility.

## Support

For API support and bug reports, contact the NeuroFab development team or file an issue in the project repository.
