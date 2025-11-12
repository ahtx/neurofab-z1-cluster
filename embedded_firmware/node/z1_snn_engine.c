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
// Neuron Table Parsing
// ============================================================================

/**
 * Parse a single 256-byte neuron entry from PSRAM
 * 
 * Binary format (256 bytes):
 *   [0-1]   uint16_t neuron_id (global)
 *   [2-3]   uint16_t synapse_count
 *   [4-7]   float threshold
 *   [8-11]  float leak_rate
 *   [12-15] uint32_t refractory_period_us
 *   [16-239] Synapse table (up to 28 synapses, 8 bytes each)
 *     [0-1]   uint16_t source_neuron_id (global)
 *     [2-5]   float weight
 *     [6-7]   uint16_t delay_us
 *   [240-255] Reserved
 */
static bool parse_neuron_entry(const uint8_t* data, z1_neuron_t* neuron) {
    if (!data || !neuron) return false;
    
    // Parse neuron header
    memcpy(&neuron->neuron_id, data + 0, 2);
    memcpy(&neuron->synapse_count, data + 2, 2);
    memcpy(&neuron->threshold, data + 4, 4);
    memcpy(&neuron->leak_rate, data + 8, 4);
    memcpy(&neuron->refractory_period_us, data + 12, 4);
    
    // Validate
    if (neuron->synapse_count > Z1_MAX_SYNAPSES_PER_NEURON) {
        return false;
    }
    
    // Initialize state
    neuron->membrane_potential = 0.0f;
    neuron->last_spike_time_us = 0;
    neuron->refractory_until_us = 0;
    neuron->spike_count = 0;
    
    // Parse synapses
    const uint8_t* synapse_data = data + 16;
    for (uint16_t i = 0; i < neuron->synapse_count; i++) {
        z1_synapse_t* syn = &neuron->synapses[i];
        memcpy(&syn->source_neuron_id, synapse_data + (i * 8) + 0, 2);
        memcpy(&syn->weight, synapse_data + (i * 8) + 2, 4);
        memcpy(&syn->delay_us, synapse_data + (i * 8) + 6, 2);
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
static void process_spike_to_neuron(z1_neuron_t* neuron, uint16_t source_id, 
                                    float value, uint32_t current_time_us) {
    // Find synapse from source
    z1_synapse_t* synapse = NULL;
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
    uint32_t delta_t = current_time_us - neuron->last_spike_time_us;
    apply_leak(neuron, delta_t);
    
    // Add weighted input
    neuron->membrane_potential += synapse->weight * value;
    
    // Check for spike
    if (neuron->membrane_potential >= neuron->threshold) {
        // Generate spike
        z1_spike_t spike = {
            .neuron_id = neuron->neuron_id,
            .timestamp_us = current_time_us,
            .value = 1.0f
        };
        
        // Add to outgoing queue
        spike_queue_push(spike);
        
        // Broadcast on Z1 bus
        z1_bus_broadcast_spike(neuron->neuron_id, current_time_us);
        
        // Update neuron state
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
        for (uint16_t n = 0; n < g_snn_engine.neuron_count; n++) {
            z1_neuron_t* neuron = &g_snn_engine.neurons[n];
            process_spike_to_neuron(neuron, spike.neuron_id, spike.value, current_time_us);
        }
    }
}

/**
 * Check Z1 bus for incoming spikes from other nodes
 */
static void check_bus_for_spikes(uint32_t current_time_us) {
    z1_bus_message_t msg;
    
    while (z1_bus_receive(&msg)) {
        if (msg.command == Z1_CMD_SNN_INJECT_SPIKE) {
            // Extract spike data from message
            uint16_t neuron_id;
            uint32_t timestamp;
            memcpy(&neuron_id, msg.data, 2);
            memcpy(&timestamp, msg.data + 2, 4);
            
            z1_spike_t spike = {
                .neuron_id = neuron_id,
                .timestamp_us = timestamp,
                .value = 1.0f
            };
            
            spike_queue_push(spike);
            g_snn_engine.stats.spikes_received++;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

bool z1_snn_engine_init(void) {
    if (g_engine_initialized) {
        return true;
    }
    
    memset(&g_snn_engine, 0, sizeof(z1_snn_engine_t));
    
    g_snn_engine.timestep_us = Z1_DEFAULT_TIMESTEP_US;
    g_snn_engine.current_time_us = 0;
    
    g_engine_initialized = true;
    printf("[SNN] Engine initialized\n");
    
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

bool z1_snn_engine_inject_spike(uint16_t neuron_id, float value) {
    if (!g_engine_running) {
        return false;
    }
    
    z1_spike_t spike = {
        .neuron_id = neuron_id,
        .timestamp_us = g_snn_engine.current_time_us,
        .value = value
    };
    
    if (spike_queue_push(spike)) {
        g_snn_engine.stats.spikes_injected++;
        return true;
    }
    
    return false;
}

bool z1_snn_engine_get_stats(z1_snn_stats_t* stats) {
    if (!stats) {
        return false;
    }
    
    *stats = g_snn_engine.stats;
    return true;
}

bool z1_snn_engine_get_neuron_state(uint16_t neuron_id, z1_neuron_state_t* state) {
    if (!state) {
        return false;
    }
    
    // Find neuron
    for (uint16_t i = 0; i < g_snn_engine.neuron_count; i++) {
        if (g_snn_engine.neurons[i].neuron_id == neuron_id) {
            z1_neuron_t* neuron = &g_snn_engine.neurons[i];
            
            state->neuron_id = neuron->neuron_id;
            state->membrane_potential = neuron->membrane_potential;
            state->spike_count = neuron->spike_count;
            state->last_spike_time_us = neuron->last_spike_time_us;
            state->is_refractory = (g_snn_engine.current_time_us < neuron->refractory_until_us);
            
            return true;
        }
    }
    
    return false;  // Neuron not found
}

uint16_t z1_snn_engine_get_neuron_count(void) {
    return g_snn_engine.neuron_count;
}

void z1_snn_engine_set_timestep(uint32_t timestep_us) {
    g_snn_engine.timestep_us = timestep_us;
}

uint32_t z1_snn_engine_get_current_time(void) {
    return g_snn_engine.current_time_us;
}

// ============================================================================
// Z1 Bus Command Handler
// ============================================================================

/**
 * Handle SNN-related commands from Z1 bus
 * Called by main firmware when SNN command is received
 */
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
    printf("========================\n\n");
}

void z1_snn_engine_print_neurons(void) {
    printf("\n=== Neuron States ===\n");
    for (uint16_t i = 0; i < g_snn_engine.neuron_count; i++) {
        z1_neuron_t* n = &g_snn_engine.neurons[i];
        printf("Neuron %u: V_mem=%.3f, threshold=%.3f, spikes=%lu, synapses=%u\n",
               n->neuron_id, n->membrane_potential, n->threshold, 
               n->spike_count, n->synapse_count);
    }
    printf("====================\n\n");
}
