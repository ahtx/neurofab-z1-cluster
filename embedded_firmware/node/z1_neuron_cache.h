/**
 * Z1 Neuron Cache
 * 
 * LRU cache for streaming neurons from PSRAM to RAM.
 */

#ifndef Z1_NEURON_CACHE_H
#define Z1_NEURON_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "z1_snn_engine.h"

// ============================================================================
// Configuration
// ============================================================================

#define Z1_NEURON_CACHE_SIZE 16  // Number of neurons to cache in RAM

// ============================================================================
// Cache Structure
// ============================================================================

typedef struct {
    z1_neuron_t neurons[Z1_NEURON_CACHE_SIZE];  // Cached neurons
    uint16_t neuron_ids[Z1_NEURON_CACHE_SIZE];  // Which neurons are cached
    bool valid[Z1_NEURON_CACHE_SIZE];           // Cache entry valid flags
    bool dirty[Z1_NEURON_CACHE_SIZE];           // Cache entry dirty flags
    uint8_t lru_counter[Z1_NEURON_CACHE_SIZE];  // LRU eviction tracking
    
    // Statistics
    uint32_t hits;        // Cache hits
    uint32_t misses;      // Cache misses
    uint32_t evictions;   // Cache evictions
} z1_neuron_cache_t;

typedef struct {
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
    float hit_rate;
    uint16_t entries_used;
} z1_neuron_cache_stats_t;

// ============================================================================
// Functions
// ============================================================================

/**
 * Initialize neuron cache
 */
void z1_neuron_cache_init(void);

/**
 * Get neuron from cache (load from PSRAM if not present)
 * 
 * @param neuron_id Local neuron ID
 * @return Pointer to cached neuron, or NULL on error
 */
z1_neuron_t* z1_neuron_cache_get(uint16_t neuron_id);

/**
 * Mark cached neuron as dirty (needs flush to PSRAM)
 * 
 * @param neuron_id Local neuron ID
 */
void z1_neuron_cache_mark_dirty(uint16_t neuron_id);

/**
 * Flush specific neuron to PSRAM
 * 
 * @param neuron_id Local neuron ID
 * @return true if successful
 */
bool z1_neuron_cache_flush(uint16_t neuron_id);

/**
 * Flush all dirty entries to PSRAM
 * 
 * @return true if all flushes successful
 */
bool z1_neuron_cache_flush_all(void);

/**
 * Invalidate cache entry
 * 
 * @param neuron_id Local neuron ID
 */
void z1_neuron_cache_invalidate(uint16_t neuron_id);

/**
 * Clear entire cache
 */
void z1_neuron_cache_clear(void);

/**
 * Get cache statistics
 * 
 * @param stats Pointer to structure to fill
 */
void z1_neuron_cache_get_stats(z1_neuron_cache_stats_t* stats);

/**
 * Print cache statistics
 */
void z1_neuron_cache_print_stats(void);

#endif // Z1_NEURON_CACHE_H
