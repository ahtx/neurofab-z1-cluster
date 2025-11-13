/**
 * Z1 Neuron Cache
 * 
 * LRU cache for streaming neurons from PSRAM to RAM.
 * Keeps only active neurons in RAM to reduce memory footprint.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_neuron_cache.h"
#include "z1_psram_neurons.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global Cache
// ============================================================================

static z1_neuron_cache_t g_cache = {0};

// ============================================================================
// Cache Management
// ============================================================================

/**
 * Initialize neuron cache
 */
void z1_neuron_cache_init(void) {
    memset(&g_cache, 0, sizeof(g_cache));
    
    // Mark all entries as invalid
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        g_cache.valid[i] = false;
        g_cache.dirty[i] = false;
        g_cache.neuron_ids[i] = 0xFFFF;  // Invalid ID
        g_cache.lru_counter[i] = 0;
    }
    
    g_cache.hits = 0;
    g_cache.misses = 0;
    g_cache.evictions = 0;
    
    printf("[Neuron Cache] Initialized: %d entries, %zu bytes\n",
           Z1_NEURON_CACHE_SIZE, sizeof(g_cache));
}

/**
 * Find neuron in cache
 * Returns cache index if found, -1 if not found
 */
static int find_in_cache(uint16_t neuron_id) {
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (g_cache.valid[i] && g_cache.neuron_ids[i] == neuron_id) {
            return i;
        }
    }
    return -1;
}

/**
 * Find free cache entry
 * Returns cache index if found, -1 if cache is full
 */
static int find_free_entry(void) {
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (!g_cache.valid[i]) {
            return i;
        }
    }
    return -1;
}

/**
 * Find LRU (Least Recently Used) entry for eviction
 */
static int find_lru_entry(void) {
    int lru_index = 0;
    uint8_t min_counter = g_cache.lru_counter[0];
    
    for (int i = 1; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (g_cache.lru_counter[i] < min_counter) {
            min_counter = g_cache.lru_counter[i];
            lru_index = i;
        }
    }
    
    return lru_index;
}

/**
 * Update LRU counters (increment all, reset accessed entry)
 */
static void update_lru(int accessed_index) {
    // Increment all counters
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (g_cache.lru_counter[i] < 255) {
            g_cache.lru_counter[i]++;
        }
    }
    
    // Reset accessed entry to 0 (most recently used)
    g_cache.lru_counter[accessed_index] = 0;
}

/**
 * Flush cache entry to PSRAM if dirty
 */
static bool flush_entry(int cache_index) {
    if (!g_cache.valid[cache_index] || !g_cache.dirty[cache_index]) {
        return true;  // Nothing to flush
    }
    
    uint16_t neuron_id = g_cache.neuron_ids[cache_index];
    
    if (!z1_psram_write_neuron(neuron_id, &g_cache.neurons[cache_index])) {
        printf("[Neuron Cache] ERROR: Failed to flush neuron %d\n", neuron_id);
        return false;
    }
    
    g_cache.dirty[cache_index] = false;
    return true;
}

/**
 * Evict LRU entry from cache
 */
static bool evict_lru(void) {
    int lru_index = find_lru_entry();
    
    // Flush if dirty
    if (!flush_entry(lru_index)) {
        return false;
    }
    
    // Mark as invalid
    g_cache.valid[lru_index] = false;
    g_cache.neuron_ids[lru_index] = 0xFFFF;
    g_cache.evictions++;
    
    return true;
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Get neuron from cache (load from PSRAM if not present)
 */
z1_neuron_t* z1_neuron_cache_get(uint16_t neuron_id) {
    // Check if already in cache
    int cache_index = find_in_cache(neuron_id);
    
    if (cache_index >= 0) {
        // Cache hit
        g_cache.hits++;
        update_lru(cache_index);
        return &g_cache.neurons[cache_index];
    }
    
    // Cache miss - need to load from PSRAM
    g_cache.misses++;
    
    // Find free entry or evict LRU
    cache_index = find_free_entry();
    if (cache_index < 0) {
        // Cache full, evict LRU
        if (!evict_lru()) {
            printf("[Neuron Cache] ERROR: Failed to evict LRU entry\n");
            return NULL;
        }
        cache_index = find_free_entry();
        if (cache_index < 0) {
            printf("[Neuron Cache] ERROR: No free entry after eviction\n");
            return NULL;
        }
    }
    
    // Load neuron from PSRAM
    if (!z1_psram_read_neuron(neuron_id, &g_cache.neurons[cache_index])) {
        printf("[Neuron Cache] ERROR: Failed to load neuron %d from PSRAM\n", neuron_id);
        return NULL;
    }
    
    // Mark entry as valid
    g_cache.valid[cache_index] = true;
    g_cache.dirty[cache_index] = false;
    g_cache.neuron_ids[cache_index] = neuron_id;
    update_lru(cache_index);
    
    return &g_cache.neurons[cache_index];
}

/**
 * Mark cached neuron as dirty (needs flush to PSRAM)
 */
void z1_neuron_cache_mark_dirty(uint16_t neuron_id) {
    int cache_index = find_in_cache(neuron_id);
    if (cache_index >= 0) {
        g_cache.dirty[cache_index] = true;
    }
}

/**
 * Flush specific neuron to PSRAM
 */
bool z1_neuron_cache_flush(uint16_t neuron_id) {
    int cache_index = find_in_cache(neuron_id);
    if (cache_index < 0) {
        return true;  // Not in cache, nothing to flush
    }
    
    return flush_entry(cache_index);
}

/**
 * Flush all dirty entries to PSRAM
 */
bool z1_neuron_cache_flush_all(void) {
    bool success = true;
    
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (g_cache.valid[i] && g_cache.dirty[i]) {
            if (!flush_entry(i)) {
                success = false;
            }
        }
    }
    
    return success;
}

/**
 * Invalidate cache entry
 */
void z1_neuron_cache_invalidate(uint16_t neuron_id) {
    int cache_index = find_in_cache(neuron_id);
    if (cache_index >= 0) {
        // Flush if dirty before invalidating
        flush_entry(cache_index);
        g_cache.valid[cache_index] = false;
        g_cache.neuron_ids[cache_index] = 0xFFFF;
    }
}

/**
 * Clear entire cache
 */
void z1_neuron_cache_clear(void) {
    // Flush all dirty entries
    z1_neuron_cache_flush_all();
    
    // Mark all as invalid
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        g_cache.valid[i] = false;
        g_cache.neuron_ids[i] = 0xFFFF;
    }
}

/**
 * Get cache statistics
 */
void z1_neuron_cache_get_stats(z1_neuron_cache_stats_t* stats) {
    if (!stats) return;
    
    stats->hits = g_cache.hits;
    stats->misses = g_cache.misses;
    stats->evictions = g_cache.evictions;
    
    uint32_t total_accesses = g_cache.hits + g_cache.misses;
    if (total_accesses > 0) {
        stats->hit_rate = (float)g_cache.hits / total_accesses;
    } else {
        stats->hit_rate = 0.0f;
    }
    
    // Count valid entries
    stats->entries_used = 0;
    for (int i = 0; i < Z1_NEURON_CACHE_SIZE; i++) {
        if (g_cache.valid[i]) {
            stats->entries_used++;
        }
    }
}

/**
 * Print cache statistics
 */
void z1_neuron_cache_print_stats(void) {
    z1_neuron_cache_stats_t stats;
    z1_neuron_cache_get_stats(&stats);
    
    printf("[Neuron Cache] Stats:\n");
    printf("  Hits:       %u\n", (unsigned int)stats.hits);
    printf("  Misses:     %u\n", (unsigned int)stats.misses);
    printf("  Evictions:  %u\n", (unsigned int)stats.evictions);
    printf("  Hit Rate:   %.1f%%\n", stats.hit_rate * 100.0f);
    printf("  Entries:    %d / %d\n", stats.entries_used, Z1_NEURON_CACHE_SIZE);
}
