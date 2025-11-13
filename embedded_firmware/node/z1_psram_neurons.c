/**
 * Z1 PSRAM Neuron Storage
 * 
 * Implements PSRAM-based neuron table storage and access for memory-efficient
 * SNN execution on RP2350B with 8MB PSRAM.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_psram_neurons.h"
#include "psram_rp2350.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Constants
// ============================================================================

#define Z1_NEURON_ENTRY_SIZE 256  // Each neuron occupies 256 bytes in PSRAM

// ============================================================================
// PSRAM Neuron Table
// ============================================================================

static z1_psram_neuron_table_t g_neuron_table = {0};

/**
 * Initialize PSRAM neuron table
 */
bool z1_psram_neuron_table_init(uint32_t base_addr, uint16_t max_neurons) {
    if (base_addr + (max_neurons * Z1_NEURON_ENTRY_SIZE) > (8 * 1024 * 1024)) {  // 8MB PSRAM
        printf("[PSRAM Neurons] ERROR: Table exceeds PSRAM size\n");
        return false;
    }
    
    g_neuron_table.base_addr = base_addr;
    g_neuron_table.neuron_count = 0;
    g_neuron_table.max_neurons = max_neurons;
    g_neuron_table.entry_size = Z1_NEURON_ENTRY_SIZE;
    
    printf("[PSRAM Neurons] Initialized: base=0x%08X, max=%d neurons\n",
           (unsigned int)base_addr, max_neurons);
    
    return true;
}

/**
 * Get PSRAM address for a neuron
 */
static inline uint32_t get_neuron_addr(uint16_t neuron_id) {
    return g_neuron_table.base_addr + (neuron_id * Z1_NEURON_ENTRY_SIZE);
}

// ============================================================================
// Neuron Serialization
// ============================================================================

/**
 * Parse neuron from PSRAM binary format to runtime structure
 */
bool z1_psram_parse_neuron(const uint8_t* data, z1_neuron_t* neuron) {
    if (!data || !neuron) return false;
    
    // Parse neuron state (offset 0-15)
    memcpy(&neuron->neuron_id, data + 0, 2);
    memcpy(&neuron->flags, data + 2, 2);
    memcpy(&neuron->membrane_potential, data + 4, 4);
    memcpy(&neuron->threshold, data + 8, 4);
    memcpy(&neuron->last_spike_time_us, data + 12, 4);
    
    // Parse synapse metadata (offset 16-23)
    memcpy(&neuron->synapse_count, data + 16, 2);
    
    // Parse neuron parameters (offset 24-31)
    memcpy(&neuron->leak_rate, data + 24, 4);
    memcpy(&neuron->refractory_period_us, data + 28, 4);
    
    // Validate synapse count
    if (neuron->synapse_count > Z1_MAX_SYNAPSES_PER_NEURON) {
        printf("[PSRAM Neurons] ERROR: Neuron %d has invalid synapse count %d\n",
               neuron->neuron_id, neuron->synapse_count);
        return false;
    }
    
    // Parse synapses (offset 40-279)
    for (uint16_t i = 0; i < neuron->synapse_count; i++) {
        uint32_t synapse_packed;
        memcpy(&synapse_packed, data + 40 + (i * 4), 4);
        
        // Extract source neuron ID (bits 31:8)
        neuron->synapses[i].source_neuron_id = synapse_packed >> 8;
        
        // Extract and decode weight (bits 7:0)
        uint8_t weight_encoded = synapse_packed & 0xFF;
        if (weight_encoded >= 128) {
            // Negative weight: 128-255 → -0.01 to -2.0
            neuron->synapses[i].weight = -(weight_encoded - 128) / 63.5f;
        } else {
            // Positive weight: 0-127 → 0.0 to 2.0
            neuron->synapses[i].weight = weight_encoded / 63.5f;
        }
        
        neuron->synapses[i].delay_us = 0;  // No delay in current version
    }
    
    // Initialize runtime state
    neuron->refractory_until_us = 0;
    neuron->spike_count = 0;
    
    return true;
}

/**
 * Serialize neuron from runtime structure to PSRAM binary format
 */
bool z1_psram_serialize_neuron(const z1_neuron_t* neuron, uint8_t* data) {
    if (!neuron || !data) return false;
    
    // Clear buffer
    memset(data, 0, Z1_NEURON_ENTRY_SIZE);
    
    // Serialize neuron state (offset 0-15)
    memcpy(data + 0, &neuron->neuron_id, 2);
    memcpy(data + 2, &neuron->flags, 2);
    memcpy(data + 4, &neuron->membrane_potential, 4);
    memcpy(data + 8, &neuron->threshold, 4);
    memcpy(data + 12, &neuron->last_spike_time_us, 4);
    
    // Serialize synapse metadata (offset 16-23)
    memcpy(data + 16, &neuron->synapse_count, 2);
    uint16_t synapse_capacity = Z1_MAX_SYNAPSES_PER_NEURON;
    memcpy(data + 18, &synapse_capacity, 2);
    
    // Serialize neuron parameters (offset 24-31)
    memcpy(data + 24, &neuron->leak_rate, 4);
    memcpy(data + 28, &neuron->refractory_period_us, 4);
    
    // Serialize synapses (offset 40-279)
    for (uint16_t i = 0; i < neuron->synapse_count; i++) {
        // Encode weight to 8-bit
        uint8_t weight_encoded;
        float weight = neuron->synapses[i].weight;
        if (weight < 0) {
            // Negative weight: -2.0 to -0.01 → 128-255
            weight_encoded = 128 + (uint8_t)(-weight * 63.5f);
        } else {
            // Positive weight: 0.0 to 2.0 → 0-127
            weight_encoded = (uint8_t)(weight * 63.5f);
        }
        
        // Pack synapse: [source_id:24][weight:8]
        uint32_t synapse_packed = (neuron->synapses[i].source_neuron_id << 8) | weight_encoded;
        memcpy(data + 40 + (i * 4), &synapse_packed, 4);
    }
    
    return true;
}

// ============================================================================
// PSRAM Access Functions
// ============================================================================

/**
 * Read neuron from PSRAM
 */
bool z1_psram_read_neuron(uint16_t neuron_id, z1_neuron_t* neuron) {
    if (neuron_id >= g_neuron_table.max_neurons) {
        printf("[PSRAM Neurons] ERROR: Neuron ID %d out of range\n", neuron_id);
        return false;
    }
    
    if (!neuron) return false;
    
    // Read neuron data from PSRAM
    uint32_t addr = get_neuron_addr(neuron_id);
    uint8_t buffer[Z1_NEURON_ENTRY_SIZE];
    
    if (!psram_read(addr, buffer, Z1_NEURON_ENTRY_SIZE)) {
        printf("[PSRAM Neurons] ERROR: Failed to read neuron %d from PSRAM\n", neuron_id);
        return false;
    }
    
    // Parse binary format to runtime structure
    if (!z1_psram_parse_neuron(buffer, neuron)) {
        printf("[PSRAM Neurons] ERROR: Failed to parse neuron %d\n", neuron_id);
        return false;
    }
    
    return true;
}

/**
 * Write neuron to PSRAM
 */
bool z1_psram_write_neuron(uint16_t neuron_id, const z1_neuron_t* neuron) {
    if (neuron_id >= g_neuron_table.max_neurons) {
        printf("[PSRAM Neurons] ERROR: Neuron ID %d out of range\n", neuron_id);
        return false;
    }
    
    if (!neuron) return false;
    
    // Serialize runtime structure to binary format
    uint8_t buffer[Z1_NEURON_ENTRY_SIZE];
    if (!z1_psram_serialize_neuron(neuron, buffer)) {
        printf("[PSRAM Neurons] ERROR: Failed to serialize neuron %d\n", neuron_id);
        return false;
    }
    
    // Write to PSRAM
    uint32_t addr = get_neuron_addr(neuron_id);
    if (!psram_write(addr, buffer, Z1_NEURON_ENTRY_SIZE)) {
        printf("[PSRAM Neurons] ERROR: Failed to write neuron %d to PSRAM\n", neuron_id);
        return false;
    }
    
    return true;
}

/**
 * Bulk load neuron table from PSRAM
 */
bool z1_psram_load_neuron_table(uint32_t source_addr, uint16_t neuron_count) {
    if (neuron_count > g_neuron_table.max_neurons) {
        printf("[PSRAM Neurons] ERROR: Neuron count %d exceeds max %d\n",
               neuron_count, g_neuron_table.max_neurons);
        return false;
    }
    
    // Copy neuron table data to our managed area
    uint32_t table_size = neuron_count * Z1_NEURON_ENTRY_SIZE;
    
    printf("[PSRAM Neurons] Loading %d neurons from 0x%08X to 0x%08X (%u bytes)\n",
           neuron_count, (unsigned int)source_addr, 
           (unsigned int)g_neuron_table.base_addr, (unsigned int)table_size);
    
    // Read in chunks to avoid large stack allocation
    #define CHUNK_SIZE 1024
    uint8_t chunk_buffer[CHUNK_SIZE];
    uint32_t bytes_remaining = table_size;
    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;
    
    while (bytes_remaining > 0) {
        uint32_t chunk_len = (bytes_remaining < CHUNK_SIZE) ? bytes_remaining : CHUNK_SIZE;
        
        if (!psram_read(source_addr + src_offset, chunk_buffer, chunk_len)) {
            printf("[PSRAM Neurons] ERROR: Failed to read chunk at offset %u\n", 
                   (unsigned int)src_offset);
            return false;
        }
        
        if (!psram_write(g_neuron_table.base_addr + dst_offset, chunk_buffer, chunk_len)) {
            printf("[PSRAM Neurons] ERROR: Failed to write chunk at offset %u\n",
                   (unsigned int)dst_offset);
            return false;
        }
        
        src_offset += chunk_len;
        dst_offset += chunk_len;
        bytes_remaining -= chunk_len;
    }
    
    g_neuron_table.neuron_count = neuron_count;
    
    printf("[PSRAM Neurons] Successfully loaded %d neurons\n", neuron_count);
    
    return true;
}

/**
 * Get neuron table info
 */
void z1_psram_get_table_info(z1_psram_neuron_table_t* info) {
    if (info) {
        *info = g_neuron_table;
    }
}
