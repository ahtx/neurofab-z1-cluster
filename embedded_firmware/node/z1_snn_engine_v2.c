/**
 * Z1 Neuromorphic Compute Node - SNN Execution Engine V2
 * 
 * PSRAM-based streaming architecture with neuron caching.
 * Reduces RAM usage from 655KB to ~15KB.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_snn_engine.h"
#include "z1_psram_neurons.h"
#include "z1_neuron_cache.h"
#include "z1_matrix_bus.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Configuration
// ============================================================================

#define Z1_SNN_TIMESTEP_US 1000  // 1ms timestep

// Reduce spike queue size to save RAM
#undef Z1_MAX_SPIKE_QUEUE_SIZE
#define Z1_MAX_SPIKE_QUEUE_SIZE 128

// ============================================================================
// Global State
// ============================================================================

typedef struct {
    bool initialized;
    bool running;
    uint8_t node_id;
    uint16_t neuron_count;
    uint32_t current_time_us;
    uint32_t timestep_us;
    
    // Statistics
    uint32_t total_spikes;
    uint32_t spikes_generated;
    uint32_t spikes_received;
    uint32_t spikes_processed;
} z1_snn_state_t;

static z1_snn_state_t g_snn_state = {0};

// Reduced spike queue (internal types)
typedef struct {
    uint32_t global_neuron_id;
    uint32_t timestamp_us;
    uint8_t flags;
} z1_spike_event_internal_t;

typedef struct {
    z1_spike_event_internal_t events[Z1_MAX_SPIKE_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} z1_spike_queue_internal_t;

static z1_spike_queue_internal_t g_spike_queue = {0};

// ============================================================================
// Spike Queue Management
// ============================================================================

static bool spike_queue_push(uint32_t global_neuron_id, uint32_t timestamp_us, uint8_t flags) {
    if (g_spike_queue.count >= Z1_MAX_SPIKE_QUEUE_SIZE) {
        printf("[SNN] WARNING: Spike queue full, dropping spike\n");
        return false;
    }
    
    g_spike_queue.events[g_spike_queue.tail].global_neuron_id = global_neuron_id;
    g_spike_queue.events[g_spike_queue.tail].timestamp_us = timestamp_us;
    g_spike_queue.events[g_spike_queue.tail].flags = flags;
    
    g_spike_queue.tail = (g_spike_queue.tail + 1) % Z1_MAX_SPIKE_QUEUE_SIZE;
    g_spike_queue.count++;
    
    return true;
}

static bool spike_queue_pop(z1_spike_event_internal_t* event) {
    if (g_spike_queue.count == 0) {
        return false;
    }
    
    if (event) {
        *event = g_spike_queue.events[g_spike_queue.head];
    }
    
    g_spike_queue.head = (g_spike_queue.head + 1) % Z1_MAX_SPIKE_QUEUE_SIZE;
    g_spike_queue.count--;
    
    return true;
}

// ============================================================================
// SNN Engine Functions
// ============================================================================

/**
 * Initialize SNN engine
 */
bool z1_snn_engine_init(uint8_t node_id) {
    if (g_snn_state.initialized) {
        printf("[SNN] Already initialized\n");
        return true;
    }
    
    printf("[SNN] Initializing engine for node %d\n", node_id);
    
    // Initialize state
    memset(&g_snn_state, 0, sizeof(g_snn_state));
    g_snn_state.node_id = node_id;
    g_snn_state.timestep_us = Z1_SNN_TIMESTEP_US;
    
    // Initialize PSRAM neuron table (base address 0x100000, max 1024 neurons)
    if (!z1_psram_neuron_table_init(0x100000, 1024)) {
        printf("[SNN] ERROR: Failed to initialize PSRAM neuron table\n");
        return false;
    }
    
    // Initialize neuron cache
    z1_neuron_cache_init();
    
    // Initialize spike queue
    memset(&g_spike_queue, 0, sizeof(g_spike_queue));
    
    g_snn_state.initialized = true;
    
    printf("[SNN] Engine initialized successfully\n");
    printf("[SNN] RAM usage: ~15 KB (cache + queue + state)\n");
    printf("[SNN] PSRAM capacity: 1024 neurons (256 KB)\n");
    
    return true;
}

/**
 * Load neuron network from PSRAM
 */
bool z1_snn_engine_load_network(uint32_t table_addr, uint16_t neuron_count) {
    if (!g_snn_state.initialized) {
        printf("[SNN] ERROR: Engine not initialized\n");
        return false;
    }
    
    if (neuron_count > 1024) {
        printf("[SNN] ERROR: Neuron count %d exceeds max 1024\n", neuron_count);
        return false;
    }
    
    printf("[SNN] Loading network: %d neurons from PSRAM 0x%08X\n",
           neuron_count, (unsigned int)table_addr);
    
    // Load neuron table from PSRAM
    if (!z1_psram_load_neuron_table(table_addr, neuron_count)) {
        printf("[SNN] ERROR: Failed to load neuron table\n");
        return false;
    }
    
    g_snn_state.neuron_count = neuron_count;
    
    // Clear cache (will be populated on-demand)
    z1_neuron_cache_clear();
    
    printf("[SNN] Network loaded: %d neurons\n", neuron_count);
    
    return true;
}

/**
 * Start SNN execution
 */
bool z1_snn_engine_start(void) {
    if (!g_snn_state.initialized) {
        printf("[SNN] ERROR: Engine not initialized\n");
        return false;
    }
    
    if (g_snn_state.neuron_count == 0) {
        printf("[SNN] ERROR: No network loaded\n");
        return false;
    }
    
    g_snn_state.running = true;
    g_snn_state.current_time_us = 0;
    
    printf("[SNN] Started: %d neurons, timestep=%u us\n",
           g_snn_state.neuron_count, (unsigned int)g_snn_state.timestep_us);
    
    return true;
}

/**
 * Stop SNN execution
 */
void z1_snn_engine_stop(void) {
    g_snn_state.running = false;
    
    // Flush all dirty cache entries to PSRAM
    z1_neuron_cache_flush_all();
    
    printf("[SNN] Stopped\n");
    z1_neuron_cache_print_stats();
}

/**
 * Check if engine is running
 */
bool z1_snn_engine_is_running(void) {
    return g_snn_state.running;
}

/**
 * Process single neuron (apply leak, check threshold)
 */
static void process_neuron(z1_neuron_t* neuron, uint32_t current_time_us) {
    // Check refractory period
    if (current_time_us < neuron->refractory_until_us) {
        return;  // Still in refractory period
    }
    
    // Apply membrane leak
    if (neuron->membrane_potential > 0.0f) {
        neuron->membrane_potential *= (1.0f - neuron->leak_rate);
        
        // Clamp to zero if very small
        if (neuron->membrane_potential < 0.001f) {
            neuron->membrane_potential = 0.0f;
        }
    }
    
    // Check for spike
    if (neuron->membrane_potential >= neuron->threshold) {
        // Generate spike
        neuron->last_spike_time_us = current_time_us;
        neuron->spike_count++;
        neuron->membrane_potential = 0.0f;  // Reset
        neuron->refractory_until_us = current_time_us + neuron->refractory_period_us;
        
        g_snn_state.spikes_generated++;
        
        // Send spike to connected neurons (via synapses)
        for (uint16_t i = 0; i < neuron->synapse_count; i++) {
            uint32_t target_global_id = neuron->synapses[i].source_neuron_id;
            
            // Add to spike queue for routing
            spike_queue_push(target_global_id, current_time_us, 0);
        }
    }
}

/**
 * Process single timestep
 */
void z1_snn_engine_step(uint32_t current_time_us) {
    if (!g_snn_state.running) {
        return;
    }
    
    g_snn_state.current_time_us = current_time_us;
    
    // Process pending spikes from queue
    z1_spike_event_internal_t spike;
    while (spike_queue_pop(&spike)) {
        g_snn_state.spikes_processed++;
        
        // Extract target node and local neuron ID
        uint8_t target_node = (spike.global_neuron_id >> 16) & 0xFF;
        uint16_t local_neuron_id = spike.global_neuron_id & 0xFFFF;
        
        if (target_node == g_snn_state.node_id) {
            // Local neuron - apply spike
            z1_neuron_t* neuron = z1_neuron_cache_get(local_neuron_id);
            if (neuron) {
                // Find synapse weight (simplified - use first synapse weight)
                float weight = (neuron->synapse_count > 0) ? neuron->synapses[0].weight : 1.0f;
                
                neuron->membrane_potential += weight;
                z1_neuron_cache_mark_dirty(local_neuron_id);
            }
        } else {
            // Remote neuron - route via matrix bus
            // TODO: Send spike to target node
        }
    }
    
    // Update all neurons (stream from PSRAM via cache)
    for (uint16_t i = 0; i < g_snn_state.neuron_count; i++) {
        z1_neuron_t* neuron = z1_neuron_cache_get(i);
        if (neuron) {
            process_neuron(neuron, current_time_us);
            z1_neuron_cache_mark_dirty(i);
        }
    }
    
    // Periodically flush dirty entries (every 100 timesteps)
    static uint32_t flush_counter = 0;
    if (++flush_counter >= 100) {
        z1_neuron_cache_flush_all();
        flush_counter = 0;
    }
}

/**
 * Inject external spike into neuron
 */
void z1_snn_engine_inject_spike(uint16_t local_neuron_id, float value) {
    if (!g_snn_state.running) {
        return;
    }
    
    if (local_neuron_id >= g_snn_state.neuron_count) {
        printf("[SNN] ERROR: Invalid neuron ID %d\n", local_neuron_id);
        return;
    }
    
    z1_neuron_t* neuron = z1_neuron_cache_get(local_neuron_id);
    if (neuron) {
        neuron->membrane_potential += value;
        z1_neuron_cache_mark_dirty(local_neuron_id);
        g_snn_state.spikes_received++;
    }
}

/**
 * Get engine statistics
 */
void z1_snn_engine_get_stats(uint16_t* active_neurons, uint32_t* total_spikes, uint32_t* spike_rate_hz) {
    if (active_neurons) *active_neurons = g_snn_state.neuron_count;
    if (total_spikes) *total_spikes = g_snn_state.total_spikes;
    if (spike_rate_hz) {
        if (g_snn_state.current_time_us > 0) {
            *spike_rate_hz = (g_snn_state.total_spikes * 1000000) / g_snn_state.current_time_us;
        } else {
            *spike_rate_hz = 0;
        }
    }
}

/**
 * Print engine status
 */
void z1_snn_engine_print_status(void) {
    printf("[SNN] Status:\n");
    printf("  Running:     %s\n", g_snn_state.running ? "YES" : "NO");
    printf("  Neurons:     %d\n", g_snn_state.neuron_count);
    printf("  Time:        %u us\n", (unsigned int)g_snn_state.current_time_us);
    printf("  Generated:   %u spikes\n", (unsigned int)g_snn_state.spikes_generated);
    printf("  Received:    %u spikes\n", (unsigned int)g_snn_state.spikes_received);
    printf("  Processed:   %u spikes\n", (unsigned int)g_snn_state.spikes_processed);
    printf("  Queue:       %d / %d\n", g_spike_queue.count, Z1_MAX_SPIKE_QUEUE_SIZE);
    
    z1_neuron_cache_print_stats();
}
