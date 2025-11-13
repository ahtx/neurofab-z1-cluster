# Documentation Discrepancies Found - November 13, 2025

## Summary

Comprehensive audit of documentation vs actual code revealed multiple critical discrepancies across hardware specifications, API endpoints, protocol commands, and data structures.

## 1. Hardware Pin Assignments

### Controller - W5500 Ethernet Module

**WRONG (docs/USER_GUIDE.md lines 77-80):**
- MISO = GPIO16
- MOSI = GPIO19
- SCK = GPIO18
- CS = GPIO17

**CORRECT (w5500_http_server.c lines 22-27):**
- MISO = GPIO36
- MOSI = GPIO39
- SCK = GPIO38
- CS = GPIO37
- RST = GPIO34
- INT = GPIO35

### Controller - OLED Display

**WRONG (docs/USER_GUIDE.md lines 83-84):**
- SDA = GPIO4
- SCL = GPIO5

**CORRECT (z1_display.c lines 16-17):**
- SDA = GPIO28
- SCL = GPIO29

## 2. HTTP API Endpoints

**Documented but NOT Implemented:**
- GET /api/status
- GET /api/info
- POST /api/nodes/{id}/ping
- POST /api/nodes/{id}/reset
- GET /api/snn/output
- GET /api/nodes/{id}/memory
- POST /api/nodes/{id}/memory
- POST /api/firmware/upload/{id}
- POST /api/firmware/verify/{id}
- POST /api/firmware/install/{id}

**Actually Implemented (7 routes):**
1. GET /api/nodes
2. POST /api/nodes/discover
3. GET /api/snn/status
4. POST /api/snn/start
5. POST /api/snn/stop
6. POST /api/snn/deploy
7. POST /api/snn/input

## 3. Protocol Command Values

**ALL COMMAND VALUES IN ARCHITECTURE.md ARE WRONG!**

### Documented vs Actual:

| Command | Documented | Actual | File |
|---------|-----------|--------|------|
| Z1_CMD_PING | 0x01 | 0x14 | z1_protocol.h:24 |
| Z1_CMD_RESET | 0x03 | 0x15 (RESET_NODE) | z1_protocol.h:25 |
| Z1_CMD_MEM_READ | 0x20 | 0x40 (MEM_READ_REQ) | z1_protocol.h:43 |
| Z1_CMD_MEM_WRITE | 0x21 | 0x42 | z1_protocol.h:45 |
| Z1_CMD_SNN_LOAD_TABLE | 0x30 | 0x78 | z1_protocol.h:73 |
| Z1_CMD_SNN_START | 0x31 | 0x73 | z1_protocol.h:68 |
| Z1_CMD_SNN_STOP | 0x32 | 0x74 | z1_protocol.h:69 |
| Z1_CMD_SNN_SPIKE | 0x33 | 0x70 | z1_protocol.h:65 |
| Z1_CMD_FRAME_START | 0xF0 | 0xF0 | ✓ CORRECT |
| Z1_CMD_FRAME_DATA | 0xF1 | 0xF1 | ✓ CORRECT |
| Z1_CMD_FRAME_END | 0xF2 | 0xF2 | ✓ CORRECT |

## 4. Data Structures

### Neuron Table Entry (256 bytes)

**WRONG (docs/ARCHITECTURE.md):**
- Neuron state: 32 bytes (includes leak_rate, refractory_period, spike_count)
- Synapse count: 56 synapses
- Synapse data: 224 bytes

**CORRECT (z1_snn_engine.h lines 52-70):**
- Neuron state: 16 bytes (neuron_id, flags, membrane_potential, threshold, last_spike_time_us)
- Synapse metadata: 8 bytes
- Neuron parameters: 8 bytes (leak_rate, refractory_period_us)
- Reserved: 8 bytes
- Synapse count: 60 synapses
- Synapse data: 240 bytes

### Correct Structure:
```c
typedef struct __attribute__((packed)) {
    // Neuron state (16 bytes)
    uint16_t neuron_id;
    uint16_t flags;
    float membrane_potential;
    float threshold;
    uint32_t last_spike_time_us;
    
    // Synapse metadata (8 bytes)
    uint16_t synapse_count;
    uint16_t synapse_capacity;
    uint32_t reserved1;
    
    // Neuron parameters (8 bytes)
    float leak_rate;
    uint32_t refractory_period_us;
    
    // Reserved (8 bytes)
    uint32_t reserved2[2];
    
    // Synapses (240 bytes = 60 × 4 bytes)
    uint32_t synapses[60];
} z1_neuron_entry_t;
```

## Files Requiring Updates

1. **docs/USER_GUIDE.md** - Fix pin assignments (lines 77-84, 110-112)
2. **docs/ARCHITECTURE.md** - Fix protocol commands (entire command table)
3. **docs/ARCHITECTURE.md** - Fix neuron structure (lines ~700-750)
4. **docs/API_REFERENCE.md** - Mark unimplemented endpoints clearly
5. **README.md** - Verify pin references match actual code

## Priority

**P0 - Critical:**
- Pin assignments (could cause hardware damage or non-function)
- Protocol commands (communication will fail)

**P1 - High:**
- Data structures (deployment will fail)
- API endpoints (user confusion)

**P2 - Medium:**
- Documentation consistency
