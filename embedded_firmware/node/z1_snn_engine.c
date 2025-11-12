/**
 * Z1 Neuromorphic Compute Node - SNN Execution Engine
 * 
 * Implements Leaky Integrate-and-Fire (LIF) neuron model with spike processing
 * for distributed spiking neural network execution on RP2350B nodes.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_snn_engine.h"
#include "psram_rp2350.h"
#include "z1_matrix_bus.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

// ============================================================================
// Global State
// ============================================================================

static z1_snn_engine_t g_snn_engine;
static bool g_engine_initialized = false;
static bool g_engine_running = false;

// ============================================================================
// Weight Conversion
// ============================================================================

/**
 * Decode 8-bit weight to float
 * 
 * Encoding:
 *   0-127:   Positive weights (0.0 to 2.0)
 *   128-255: Negative weights (-0.01 to -2.0)
 */
static inline float decode_weight(uint8_t weight_int) {
    if (weight_int >= 128) {
        // Negative weight: 128-255 → -0.01 to -2.0
        return -(weight_int - 128) / 63.5f;
    } else {
        // Positive weight: 0-127 → 0.0 to 2.0
        return weight_int / 63.5f;
    }
}

// ============================================================================
// Neuron Table Parsing
// ============================================================================

/**
 * Parse a single 256-byte neuron entry from PSRAM
 * 
 * Binary format (256 bytes):
 *   Offset 0-15:   Neuron state (16 bytes)
 *     [0-1]   uint16_t neuron_id (local)
 *     [2-3]   uint16_t flags
 *     [4-7]   float membrane_potential
 *     [8-11]  float threshold
 *     [12-15] uint32_t last_spike_time_us
 * 
 *   Offset 16-23:  Synapse metadata (8 bytes)
 *     [16-17] uint16_t synapse_count
 *     [18-19] uint16_t synapse_capacity
 *     [20-23] uint32_t reserved
 * 
 *   Offset 24-31:  Neuron parameters (8 bytes)
 *     [24-27] float leak_rate
 *     [28-31] uint32_t refractory_period_us
 * 
 *   Offset 32-39:  Reserved (8 bytes)
 * 
 *   Offset 40-279: Synapses (60 × 4 bytes = 240 bytes)
 *     Each synapse: uint32_t packed as:
 *       Bits [31:8]  - Source neuron ID (24-bit, encoded as node_id << 16 | local_id)
 *       Bits [7:0]   - Weight (8-bit, 0-255)
 */
static bool parse_neuron_entry(const uint8_t* data, z1_neuron_t* neuron) {
    if (!data || !neuron) return false;
    
    // Parse neuron state (offset 0-15)
    memcpy(&neuron->neuron_id, data + 0, 2);
    memcpy(&neuron->flags, data + 2, 2);
    memcpy(&neuron->membrane_potential, data + 4, 4);
    memcpy(&neuron->threshold, data + 8, 4);
    memcpy(&neuron->last_spike_time_us, data + 12, 4);
    
    // Parse synapse metadata (offset 16-23)
    memcpy(&neuron->synapse_count, data + 16, 2);
    // synapse_capacity at offset 18 (not used in runtime)
    
    // Parse neuron parameters (offset 24-31)
    memcpy(&neuron->leak_rate, data + 24, 4);
    memcpy(&neuron->refractory_period_us, data + 28, 4);
    
    // Validate
    if (neuron->synapse_count > Z1_MAX_SYNAPSES_PER_NEURON) {
        return false;
    }
    
    // Initialize runtime state
    neuron->refractory_until_us = 0;
    neuron->spike_count = 0;
    
    // Parse synapses (offset 40+, 4 bytes each)
    const uint8_t* synapse_data = data + 40;
    for (uint16_t i = 0; i < neuron->synapse_count; i++) {
        // Read packed synapse (4 bytes)
        uint32_t synapse_packed;
        memcpy(&synapse_packed, synapse_data + (i * 4), 4);
        
        // Extract source ID (24 bits) and weight (8 bits)
        uint32_t source_id = (synapse_packed >> 8) & 0xFFFFFF;
        uint8_t weight_int = synapse_packed & 0xFF;
        
        // Store in runtime structure
        neuron->synapses[i].source_neuron_id = source_id;
        neuron->synapses[i].weight = decode_weight(weight_int);
        neuron->synapses[i].delay_us = 1000;  // Default 1ms delay
    }
    
    return true;
}

/**
 * Load neuron tables from PSRAM into engine
 */
static bool load_neuron_tables_from_psram(void) {
    uint8_t entry_buffer[Z1_NEURON_ENTRY_SIZE];
    uint32_t psram_addr = Z1_NEURON_TABLE_BASE_ADDR;
    
    g_snn_engine.neuron_count = 0;
    
    // Read neuron entries until we find an empty one (neuron_id = 0xFFFF)
    for (uint16_t i = 0; i < Z1_MAX_NEURONS_PER_NODE; i++) {
        // Read entry from PSRAM
        if (!psram_read(psram_addr, entry_buffer, Z1_NEURON_ENTRY_SIZE)) {
            printf("[SNN] Failed to read PSRAM at 0x%08lX\n", psram_addr);
            return false;
        }
        
        // Check if this is the end marker
        uint16_t neuron_id;
        memcpy(&neuron_id, entry_buffer, 2);
        if (neuron_id == 0xFFFF) {
            break;  // End of neuron table
        }
        
        // Parse neuron
        z1_neuron_t* neuron = &g_snn_engine.neurons[g_snn_engine.neuron_count];
        if (!parse_neuron_entry(entry_buffer, neuron)) {
            printf("[SNN] Failed to parse neuron entry %u\n", i);
            return false;
        }
        
        printf("[SNN] Loaded neuron %u: threshold=%.3f, synapses=%u\n",
               neuron->neuron_id, neuron->threshold, neuron->synapse_count);
        
        g_snn_engine.neuron_count++;
        psram_addr += Z1_NEURON_ENTRY_SIZE;
    }
    
    printf("[SNN] Loaded %u neurons from PSRAM\n", g_snn_engine.neuron_count);
    return g_snn_engine.neuron_count > 0;
}

// ============================================================================
// Spike Queue Management
// ============================================================================

static bool spike_queue_push(z1_spike_t spike) {
    if (g_snn_engine.spike_queue_size >= Z1_MAX_SPIKE_QUEUE_SIZE) {
        g_snn_engine.stats.spikes_dropped++;
        return false;
    }
    
    uint16_t idx = (g_snn_engine.spike_queue_head + g_snn_engine.spike_queue_size) 
                   % Z1_MAX_SPIKE_QUEUE_SIZE;
    g_snn_engine.spike_queue[idx] = spike;
    g_snn_engine.spike_queue_size++;
    
    return true;
}

static bool spike_queue_pop(z1_spike_t* spike) {
    if (g_snn_engine.spike_queue_size == 0) {
        return false;
    }
    
    *spike = g_snn_engine.spike_queue[g_snn_engine.spike_queue_head];
    g_snn_engine.spike_queue_head = (g_snn_engine.spike_queue_head + 1) 
                                     % Z1_MAX_SPIKE_QUEUE_SIZE;
    g_snn_engine.spike_queue_size--;
    
    return true;
}

// ============================================================================
// LIF Neuron Simulation
// ============================================================================

/**
 * Update neuron membrane potential with leak
 */
static void apply_leak(z1_neuron_t* neuron, uint32_t delta_t_us) {
    if (neuron->leak_rate > 0.0f) {
        float decay = expf(-(float)delta_t_us / (neuron->leak_rate * 1000000.0f));
        neuron->membrane_potential *= decay;
    }
}

/**
 * Process incoming spike to a neuron
 */
static void process_spike_to_neuron(z1_neuron_t* neuron, uint32_t source_id, 
                                    float value, uint32_t current_time_us) {
    // Find synapse from source
    z1_synapse_runtime_t* synapse = NULL;
    for (uint16_t i = 0; i < neuron->synapse_count; i++) {
        if (neuron->synapses[i].source_neuron_id == source_id) {
            synapse = &neuron->synapses[i];
            break;
        }
    }
    
    if (!synapse) {
        return;  // No synapse from this source
    }
    
    // Check refractory period
    if (current_time_us < neuron->refractory_until_us) {
        return;  // Neuron is refractory
    }
    
    // Apply leak since last update
    if (neuron->last_spike_time_us > 0) {
        uint32_t delta_t = current_time_us - neuron->last_spike_time_us;
        apply_leak(neuron, delta_t);
    }
    
    // Apply synaptic weight
    neuron->membrane_potential += synapse->weight * value;
    
    // Check for spike
    if (neuron->membrane_potential >= neuron->threshold) {
        // Generate spike
        z1_spike_t outgoing_spike = {
            .neuron_id = neuron->neuron_id,
            .timestamp_us = current_time_us,
            .value = 1.0f
        };
        
        // Send spike on bus
        z1_bus_send_spike(outgoing_spike.neuron_id, outgoing_spike.value);
        
        // Reset membrane potential and set refractory period
        neuron->membrane_potential = 0.0f;
        neuron->last_spike_time_us = current_time_us;
        neuron->refractory_until_us = current_time_us + neuron->refractory_period_us;
        neuron->spike_count++;
        
        g_snn_engine.stats.spikes_generated++;
    }
}

// ============================================================================
// Spike Processing
// ============================================================================

/**
 * Process all pending spikes in the queue
 */
static void process_spike_queue(uint32_t current_time_us) {
    uint16_t spikes_to_process = g_snn_engine.spike_queue_size;
    
    for (uint16_t i = 0; i < spikes_to_process; i++) {
        z1_spike_t spike;
        if (!spike_queue_pop(&spike)) {
            break;
        }
        
        g_snn_engine.stats.spikes_processed++;
        
        // Find all neurons that have synapses from this source
        // Source ID format: (node_id << 16) | local_neuron_id
        uint32_t spike_source_id = spike.neuron_id;  // Already encoded
        
        for (uint16_t n = 0; n < g_snn_engine.neuron_count; n++) {
            z1_neuron_t* neuron = &g_snn_engine.neurons[n];
            process_spike_to_neuron(neuron, spike_source_id, spike.value, current_time_us);
        }
    }
}

/**
 * Check Z1 bus for incoming spikes from other nodes
 */
static void check_bus_for_spikes(uint32_t current_time_us) {
    z1_bus_message_t msg;
    while (z1_bus_receive(&msg)) {
        if (msg.command == Z1_CMD_SNN_SPIKE) {
            // Extract spike data
            z1_spike_t spike;
            spike.neuron_id = (msg.source_node << 16) | msg.data[0];  // Encode as (node << 16 | local_id)
            memcpy(&spike.value, msg.data + 2, 4);
            spike.timestamp_us = current_time_us;
            
            if (spike_queue_push(spike)) {
                g_snn_engine.stats.spikes_received++;
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

bool z1_snn_engine_init(uint8_t node_id) {
    if (g_engine_initialized) {
        return true;
    }
    
    memset(&g_snn_engine, 0, sizeof(g_snn_engine));
    g_snn_engine.node_id = node_id;
    g_snn_engine.timestep_us = 1000;  // 1ms default
    
    g_engine_initialized = true;
    printf("[SNN] Engine initialized for node %u\n", node_id);
    
    return true;
}

bool z1_snn_engine_load_network(void) {
    if (!g_engine_initialized) {
        printf("[SNN] Engine not initialized\n");
        return false;
    }
    
    // Load neuron tables from PSRAM
    if (!load_neuron_tables_from_psram()) {
        printf("[SNN] Failed to load neuron tables\n");
        return false;
    }
    
    printf("[SNN] Network loaded: %u neurons\n", g_snn_engine.neuron_count);
    return true;
}

bool z1_snn_engine_start(void) {
    if (!g_engine_initialized) {
        return false;
    }
    
    if (g_snn_engine.neuron_count == 0) {
        printf("[SNN] No neurons loaded\n");
        return false;
    }
    
    g_engine_running = true;
    g_snn_engine.current_time_us = 0;
    
    printf("[SNN] Engine started with %u neurons\n", g_snn_engine.neuron_count);
    return true;
}

void z1_snn_engine_stop(void) {
    g_engine_running = false;
    printf("[SNN] Engine stopped\n");
}

bool z1_snn_engine_is_running(void) {
    return g_engine_running;
}

void z1_snn_engine_step(void) {
    if (!g_engine_running) {
        return;
    }
    
    uint32_t current_time = g_snn_engine.current_time_us;
    
    // Check for incoming spikes from bus
    check_bus_for_spikes(current_time);
    
    // Process spike queue
    process_spike_queue(current_time);
    
    // Advance time
    g_snn_engine.current_time_us += g_snn_engine.timestep_us;
}

void z1_snn_engine_inject_spike(uint16_t neuron_id, float value) {
    if (!g_engine_running) {
        return;
    }
    
    // Create spike with local neuron ID encoded as (this_node << 16 | neuron_id)
    z1_spike_t spike;
    spike.neuron_id = (g_snn_engine.node_id << 16) | neuron_id;
    spike.value = value;
    spike.timestamp_us = g_snn_engine.current_time_us;
    
    if (spike_queue_push(spike)) {
        g_snn_engine.stats.spikes_injected++;
    }
}

z1_snn_stats_t z1_snn_engine_get_stats(void) {
    return g_snn_engine.stats;
}

// ============================================================================
// Bus Command Handler
// ============================================================================

void z1_snn_handle_bus_command(z1_bus_message_t* msg) {
    if (!msg) return;
    
    switch (msg->command) {
        case Z1_CMD_SNN_LOAD_TABLE:
            // Reload neuron tables from PSRAM
            z1_snn_engine_load_network();
            break;
            
        case Z1_CMD_SNN_START:
            z1_snn_engine_start();
            break;
            
        case Z1_CMD_SNN_STOP:
            z1_snn_engine_stop();
            break;
            
        case Z1_CMD_SNN_INJECT_SPIKE:
            // Spike injection handled in check_bus_for_spikes()
            break;
            
        case Z1_CMD_SNN_GET_STATUS:
            // Send status response
            {
                uint8_t response[16];
                response[0] = g_engine_running ? 1 : 0;
                memcpy(response + 1, &g_snn_engine.neuron_count, 2);
                memcpy(response + 3, &g_snn_engine.stats.spikes_generated, 4);
                memcpy(response + 7, &g_snn_engine.stats.spikes_processed, 4);
                z1_bus_send_response(msg->source_node, response, 11);
            }
            break;
            
        default:
            break;
    }
}

// ============================================================================
// Debug and Diagnostics
// ============================================================================

void z1_snn_engine_print_status(void) {
    printf("\n=== SNN Engine Status ===\n");
    printf("Initialized: %s\n", g_engine_initialized ? "Yes" : "No");
    printf("Running: %s\n", g_engine_running ? "Yes" : "No");
    printf("Neurons loaded: %u\n", g_snn_engine.neuron_count);
    printf("Current time: %lu us\n", g_snn_engine.current_time_us);
    printf("Timestep: %lu us\n", g_snn_engine.timestep_us);
    printf("\n=== Statistics ===\n");
    printf("Spikes received: %lu\n", g_snn_engine.stats.spikes_received);
    printf("Spikes injected: %lu\n", g_snn_engine.stats.spikes_injected);
    printf("Spikes processed: %lu\n", g_snn_engine.stats.spikes_processed);
    printf("Spikes generated: %lu\n", g_snn_engine.stats.spikes_generated);
    printf("Spikes dropped: %lu\n", g_snn_engine.stats.spikes_dropped);
    printf("Queue size: %u/%u\n", g_snn_engine.spike_queue_size, Z1_MAX_SPIKE_QUEUE_SIZE);
}
