# Z1 SNN Engine - PSRAM Architecture Design

**Date:** November 13, 2025  
**Status:** Design Complete, Implementation In Progress

---

## Problem Statement

Current SNN engine uses **0.63 MB RAM** but only **0.50 MB (512 KB)** is available.

**Memory Breakdown:**
- Neuron array `[1024]`: 0.62 MB ← **Main problem**
- Spike queue `[512]`: 6 KB
- Other state: ~5 KB
- **Total: 0.63 MB**
- **Overflow: 1.25x (128 KB over limit)**

---

## Solution: PSRAM-Based Streaming Architecture

### Core Concept
- **Store all neurons in PSRAM** (8MB available)
- **Cache only active neurons in RAM** (working set)
- **Stream neurons on-demand** during processing

### Memory Layout

#### PSRAM (8 MB available)
```
Address 0x00000000: Neuron Table Start
  [0x000 - 0x0FF]  Neuron 0   (256 bytes)
  [0x100 - 0x1FF]  Neuron 1   (256 bytes)
  ...
  [0x3FF00 - 0x3FFFF] Neuron 1023 (256 bytes)

Total: 1024 neurons × 256 bytes = 262,144 bytes (256 KB)
```

#### RAM (512 KB available)
```
Active Neuron Cache:  16 neurons × 634 bytes = 10 KB
Spike Queue:          128 events × 9 bytes  = 1.2 KB
Engine State:         ~2 KB
Other buffers:        ~10 KB
─────────────────────────────────────────────────
Total RAM Usage:      ~23 KB (4.5% of available)
```

---

## Architecture Components

### 1. Neuron Cache (RAM)
```c
#define Z1_NEURON_CACHE_SIZE 16

typedef struct {
    z1_neuron_t neurons[Z1_NEURON_CACHE_SIZE];  // Cached neurons
    uint16_t neuron_ids[Z1_NEURON_CACHE_SIZE];  // Which neurons are cached
    bool valid[Z1_NEURON_CACHE_SIZE];           // Cache entry valid flags
    uint8_t lru_counter[Z1_NEURON_CACHE_SIZE];  // LRU eviction tracking
    uint16_t hits;                               // Cache hits
    uint16_t misses;                             // Cache misses
} z1_neuron_cache_t;
```

**Cache Policy:** Least Recently Used (LRU)

### 2. PSRAM Neuron Table
```c
typedef struct {
    uint32_t base_addr;      // PSRAM base address of neuron table
    uint16_t neuron_count;   // Total neurons in table
    uint16_t entry_size;     // Size of each neuron entry (256 bytes)
} z1_psram_neuron_table_t;
```

**Neuron Entry Format (256 bytes in PSRAM):**
```
Offset 0-15:   Neuron state (16 bytes)
  [0-1]   uint16_t neuron_id
  [2-3]   uint16_t flags
  [4-7]   float membrane_potential
  [8-11]  float threshold
  [12-15] uint32_t last_spike_time_us

Offset 16-23:  Synapse metadata (8 bytes)
  [16-17] uint16_t synapse_count
  [18-19] uint16_t synapse_capacity
  [20-23] uint32_t reserved

Offset 24-31:  Neuron parameters (8 bytes)
  [24-27] float leak_rate
  [28-31] uint32_t refractory_period_us

Offset 32-39:  Reserved (8 bytes)

Offset 40-279: Synapses (60 × 4 bytes = 240 bytes)
  Each synapse: uint32_t packed as:
    Bits [31:8]  - Source neuron ID (24-bit)
    Bits [7:0]   - Weight (8-bit, 0-255)
```

### 3. Reduced Spike Queue
```c
#define Z1_MAX_SPIKE_QUEUE_SIZE 128  // Reduced from 512

typedef struct {
    z1_spike_event_t events[Z1_MAX_SPIKE_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} z1_spike_queue_t;
```

**Size:** 128 × 9 bytes = 1,152 bytes (1.1 KB)

### 4. Refactored Engine State
```c
typedef struct {
    // Configuration
    uint8_t node_id;
    bool initialized;
    bool running;
    
    // PSRAM neuron table
    z1_psram_neuron_table_t neuron_table;
    
    // RAM cache for active neurons
    z1_neuron_cache_t neuron_cache;
    
    // Spike queue
    z1_spike_queue_t spike_queue;
    
    // Timing
    uint32_t current_time_us;
    uint32_t timestep_us;
    
    // Statistics
    z1_snn_stats_t stats;
} z1_snn_engine_t;
```

**Size:** ~15 KB (vs 655 KB before)

---

## Key Functions

### Neuron Cache Management

```c
/**
 * Load neuron from PSRAM into cache
 * Returns pointer to cached neuron or NULL if load fails
 */
z1_neuron_t* z1_neuron_cache_load(uint16_t neuron_id);

/**
 * Write cached neuron back to PSRAM
 */
bool z1_neuron_cache_flush(uint16_t neuron_id);

/**
 * Get neuron from cache (load if not present)
 */
z1_neuron_t* z1_neuron_cache_get(uint16_t neuron_id);

/**
 * Evict least recently used neuron from cache
 */
void z1_neuron_cache_evict_lru(void);
```

### PSRAM Access

```c
/**
 * Read neuron from PSRAM
 */
bool z1_psram_read_neuron(uint16_t neuron_id, z1_neuron_t* neuron);

/**
 * Write neuron to PSRAM
 */
bool z1_psram_write_neuron(uint16_t neuron_id, const z1_neuron_t* neuron);

/**
 * Parse neuron from PSRAM binary format
 */
bool z1_psram_parse_neuron(const uint8_t* data, z1_neuron_t* neuron);

/**
 * Serialize neuron to PSRAM binary format
 */
bool z1_psram_serialize_neuron(const z1_neuron_t* neuron, uint8_t* data);
```

---

## Processing Flow

### Initialization
```
1. Initialize PSRAM
2. Initialize neuron cache (all entries invalid)
3. Set PSRAM neuron table base address
4. Load first 16 neurons into cache (warm cache)
```

### Timestep Processing
```
For each timestep:
  1. Process spike queue:
     For each pending spike:
       a. Get target neuron (cache load if needed)
       b. Apply spike to neuron
       c. Check if neuron fires
       d. If fires, generate output spike
       
  2. Update all neurons:
     For neuron_id in 0..neuron_count:
       a. Get neuron from cache (load if needed)
       b. Apply leak
       c. Check threshold
       d. Update refractory state
       e. Mark cache entry as dirty
       
  3. Flush dirty cache entries to PSRAM
```

### Cache Behavior
- **Cache hit:** Neuron already in RAM, use directly (~10 cycles)
- **Cache miss:** Load from PSRAM (~1000 cycles), evict LRU if full
- **Dirty flush:** Write modified neurons back to PSRAM periodically

---

## Performance Considerations

### Cache Hit Rate
- **Expected:** 80-90% for typical networks
- **Reason:** Neurons with many connections are accessed frequently
- **Optimization:** Pre-load highly connected neurons

### PSRAM Access Time
- **Read:** ~500ns per byte
- **256-byte neuron:** ~128μs to load
- **Cache of 16:** Can handle 16 misses per millisecond

### Throughput
- **With 90% hit rate:** ~14 cache loads per ms
- **Processing time:** ~1.8ms per timestep (1024 neurons)
- **Max frequency:** ~550 Hz timestep rate

---

## Memory Savings

### Before Refactoring
```
neurons[1024]:        655 KB
spike_queue[512]:       6 KB
engine_state:           5 KB
─────────────────────────────
Total:                666 KB  ❌ OVERFLOW (512 KB limit)
```

### After Refactoring
```
neuron_cache[16]:      10 KB  (RAM)
spike_queue[128]:       1 KB  (RAM)
engine_state:           2 KB  (RAM)
neuron_table[1024]:   256 KB  (PSRAM)
─────────────────────────────
Total RAM:             13 KB  ✅ FITS (2.5% of 512 KB)
Total PSRAM:          256 KB  ✅ FITS (3.1% of 8 MB)
```

**RAM Savings:** 666 KB → 13 KB (98% reduction!)

---

## Implementation Plan

### Phase 1: PSRAM Neuron Storage ✅
- [x] Design PSRAM layout
- [ ] Implement `z1_psram_read_neuron()`
- [ ] Implement `z1_psram_write_neuron()`
- [ ] Implement `z1_psram_parse_neuron()`
- [ ] Implement `z1_psram_serialize_neuron()`

### Phase 2: Neuron Cache
- [ ] Implement `z1_neuron_cache_t` structure
- [ ] Implement `z1_neuron_cache_init()`
- [ ] Implement `z1_neuron_cache_get()` with LRU
- [ ] Implement `z1_neuron_cache_flush()`

### Phase 3: Engine Refactoring
- [ ] Replace `neurons[1024]` with `neuron_cache[16]`
- [ ] Update `z1_snn_engine_step()` to use cache
- [ ] Update spike processing to use cache
- [ ] Add cache statistics

### Phase 4: Optimization
- [ ] Reduce spike queue to 128 entries
- [ ] Add cache warming (pre-load hot neurons)
- [ ] Add batch PSRAM operations
- [ ] Profile and optimize

---

## Testing Strategy

### Unit Tests
1. PSRAM neuron read/write
2. Cache hit/miss behavior
3. LRU eviction
4. Dirty flag tracking

### Integration Tests
1. Load 1024 neurons from PSRAM
2. Process spikes with cache
3. Verify neuron state persistence
4. Measure cache hit rate

### Performance Tests
1. Timestep processing time
2. Cache miss penalty
3. PSRAM throughput
4. Memory usage verification

---

## Risks & Mitigation

### Risk 1: Cache Thrashing
**Problem:** Too many cache misses slow down processing  
**Mitigation:** 
- Increase cache size if needed (16 → 32)
- Pre-load frequently accessed neurons
- Profile and optimize access patterns

### Risk 2: PSRAM Latency
**Problem:** PSRAM access slower than RAM  
**Mitigation:**
- Batch reads/writes
- Use DMA for transfers
- Keep hot neurons in cache longer

### Risk 3: Cache Coherency
**Problem:** Stale data in cache vs PSRAM  
**Mitigation:**
- Dirty flag tracking
- Periodic flush
- Write-through for critical updates

---

## Success Criteria

✅ **RAM usage < 512 KB**  
✅ **Compiles successfully**  
✅ **Processes 1024 neurons**  
✅ **Timestep rate > 100 Hz**  
✅ **Cache hit rate > 70%**  
✅ **XOR network accuracy = 100%**

---

**Estimated Implementation Time:** 2-3 hours  
**Status:** Design complete, starting implementation now
