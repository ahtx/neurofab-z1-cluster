/**
 * Z1 PSRAM Neuron Storage
 * 
 * Header for PSRAM-based neuron table storage and access.
 */

#ifndef Z1_PSRAM_NEURONS_H
#define Z1_PSRAM_NEURONS_H

#include <stdint.h>
#include <stdbool.h>
#include "z1_snn_engine.h"

// ============================================================================
// PSRAM Neuron Table
// ============================================================================

/**
 * PSRAM neuron table descriptor
 */
typedef struct {
    uint32_t base_addr;      // PSRAM base address of neuron table
    uint16_t neuron_count;   // Number of neurons currently loaded
    uint16_t max_neurons;    // Maximum neurons this table can hold
    uint16_t entry_size;     // Size of each neuron entry (256 bytes)
} z1_psram_neuron_table_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize PSRAM neuron table
 * 
 * @param base_addr PSRAM base address for neuron storage
 * @param max_neurons Maximum number of neurons to support
 * @return true if successful
 */
bool z1_psram_neuron_table_init(uint32_t base_addr, uint16_t max_neurons);

// ============================================================================
// Serialization
// ============================================================================

/**
 * Parse neuron from PSRAM binary format to runtime structure
 * 
 * @param data Pointer to 256-byte PSRAM neuron entry
 * @param neuron Pointer to runtime neuron structure to fill
 * @return true if successful
 */
bool z1_psram_parse_neuron(const uint8_t* data, z1_neuron_t* neuron);

/**
 * Serialize neuron from runtime structure to PSRAM binary format
 * 
 * @param neuron Pointer to runtime neuron structure
 * @param data Pointer to 256-byte buffer to fill
 * @return true if successful
 */
bool z1_psram_serialize_neuron(const z1_neuron_t* neuron, uint8_t* data);

// ============================================================================
// PSRAM Access
// ============================================================================

/**
 * Read neuron from PSRAM
 * 
 * @param neuron_id Local neuron ID (0-1023)
 * @param neuron Pointer to neuron structure to fill
 * @return true if successful
 */
bool z1_psram_read_neuron(uint16_t neuron_id, z1_neuron_t* neuron);

/**
 * Write neuron to PSRAM
 * 
 * @param neuron_id Local neuron ID (0-1023)
 * @param neuron Pointer to neuron structure to write
 * @return true if successful
 */
bool z1_psram_write_neuron(uint16_t neuron_id, const z1_neuron_t* neuron);

/**
 * Bulk load neuron table from PSRAM
 * 
 * Copies neuron table data from source address to managed neuron table area.
 * Used when controller sends neuron table via memory write commands.
 * 
 * @param source_addr PSRAM address where neuron table was written
 * @param neuron_count Number of neurons in table
 * @return true if successful
 */
bool z1_psram_load_neuron_table(uint32_t source_addr, uint16_t neuron_count);

/**
 * Get neuron table info
 * 
 * @param info Pointer to structure to fill with table info
 */
void z1_psram_get_table_info(z1_psram_neuron_table_t* info);

#endif // Z1_PSRAM_NEURONS_H
