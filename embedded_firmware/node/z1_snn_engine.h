/**
 * Z1 SNN Execution Engine
 * 
 * Leaky Integrate-and-Fire (LIF) neuron model with spike-based communication.
 * Runs on compute nodes, processes incoming spikes and generates outbound spikes.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_SNN_ENGINE_H
#define Z1_SNN_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Configuration
// ============================================================================

#define Z1_SNN_MAX_NEURONS      4096  // Maximum neurons per node
#define Z1_SNN_MAX_SYNAPSES     60    // Maximum synapses per neuron
#define Z1_SNN_PSRAM_BASE       0x20000000  // PSRAM base address
#define Z1_SNN_NEURON_TABLE_OFFSET 0x100000 // 1MB offset for neuron table

// ============================================================================
// Neuron Data Structures
// ============================================================================

/**
 * Neuron state (16 bytes)
 */
typedef struct __attribute__((packed)) {
    uint16_t neuron_id;           // Local neuron ID (0-4095)
    uint16_t flags;               // Status flags
    float    membrane_potential;  // Current membrane potential
    float    threshold;           // Spike threshold
    uint32_t last_spike_time_us;  // Timestamp of last spike
} z1_neuron_state_t;

/**
 * Synapse entry (4 bytes)
 * 
 * Packed format:
 *   Bits [31:8]  - Source neuron global ID (24 bits)
 *   Bits [7:0]   - Weight (8-bit fixed point, 0-255)
 */
typedef uint32_t z1_synapse_t;

/**
 * Neuron table entry (256 bytes in PSRAM)
 */
typedef struct __attribute__((packed)) {
    // Neuron state (16 bytes)
    z1_neuron_state_t state;
    
    // Synapse table metadata (8 bytes)
    uint16_t synapse_count;       // Number of incoming synapses
    uint16_t synapse_capacity;    // Max synapses allocated
    uint32_t reserved1;
    
    // Neuron parameters (8 bytes)
    float    leak_rate;           // Membrane leak rate (0.0-1.0)
    uint32_t refractory_period_us; // Refractory period
    
    // Reserved for future use (8 bytes)
    uint32_t reserved2[2];
    
    // Synapse entries (60 × 4 bytes = 240 bytes)
    z1_synapse_t synapses[Z1_SNN_MAX_SYNAPSES];
} z1_neuron_entry_t;

// Neuron flags
#define Z1_NEURON_FLAG_ACTIVE       0x0001  // Neuron is active
#define Z1_NEURON_FLAG_INHIBITORY   0x0002  // Inhibitory neuron
#define Z1_NEURON_FLAG_INPUT        0x0004  // Input neuron (no processing)
#define Z1_NEURON_FLAG_OUTPUT       0x0008  // Output neuron
#define Z1_NEURON_FLAG_REFRACTORY   0x0010  // In refractory period

// ============================================================================
// Runtime Structures (in RAM)
// ============================================================================

#define Z1_MAX_NEURONS_PER_NODE 4096
#define Z1_MAX_SYNAPSES_PER_NEURON 60
#define Z1_MAX_SPIKE_QUEUE_SIZE 256
#define Z1_NEURON_ENTRY_SIZE 256
#define Z1_NEURON_TABLE_BASE_ADDR 0x20100000

/**
 * Runtime synapse structure
 */
typedef struct {
    uint32_t source_neuron_id;  // Encoded as (node_id << 16) | local_id
    float weight;               // Decoded weight (-2.0 to 2.0)
    uint16_t delay_us;          // Synaptic delay
} z1_synapse_runtime_t;

/**
 * Runtime neuron structure
 */
typedef struct {
    uint16_t neuron_id;                                    // Local neuron ID
    uint16_t flags;                                        // Status flags
    float membrane_potential;                              // Current V_mem
    float threshold;                                       // Spike threshold
    uint32_t last_spike_time_us;                           // Last spike time
    float leak_rate;                                       // Membrane leak
    uint32_t refractory_period_us;                         // Refractory period
    uint32_t refractory_until_us;                          // Refractory end time
    uint16_t synapse_count;                                // Number of synapses
    uint32_t spike_count;                                  // Total spikes generated
    z1_synapse_runtime_t synapses[Z1_MAX_SYNAPSES_PER_NEURON];  // Synapse array
} z1_neuron_t;

/**
 * Spike structure
 */
typedef struct {
    uint32_t neuron_id;      // Encoded as (node_id << 16) | local_id
    uint32_t timestamp_us;   // Spike timestamp
    float value;             // Spike value (usually 1.0)
} z1_spike_t;

/**
 * SNN statistics
 */
typedef struct {
    uint32_t spikes_received;   // Spikes from other nodes
    uint32_t spikes_injected;   // Spikes injected locally
    uint32_t spikes_processed;  // Spikes processed
    uint32_t spikes_generated;  // Spikes generated
    uint32_t spikes_dropped;    // Spikes dropped (queue full)
} z1_snn_stats_t;

/**
 * SNN engine runtime state
 */
typedef struct {
    uint8_t node_id;                                       // This node's ID
    uint16_t neuron_count;                                 // Number of neurons loaded
    uint32_t current_time_us;                              // Current simulation time
    uint32_t timestep_us;                                  // Simulation timestep
    z1_neuron_t neurons[Z1_MAX_NEURONS_PER_NODE];          // Neuron array
    z1_spike_t spike_queue[Z1_MAX_SPIKE_QUEUE_SIZE];       // Spike queue
    uint16_t spike_queue_head;                             // Queue head index
    uint16_t spike_queue_size;                             // Queue size
    z1_snn_stats_t stats;                                  // Statistics
} z1_snn_engine_t;

// ============================================================================
// SNN Engine State
// ============================================================================

typedef struct {
    bool initialized;
    bool running;
    uint8_t node_id;
    uint16_t neuron_count;
    uint32_t neuron_table_addr;   // PSRAM address of neuron table
    uint32_t current_time_us;     // Current simulation time
    uint32_t total_spikes;        // Total spikes processed
    uint32_t spikes_generated;    // Spikes generated by this node
} z1_snn_engine_state_t;

// ============================================================================
// Spike Queue (for buffering incoming spikes)
// ============================================================================

#define Z1_SPIKE_QUEUE_SIZE 256

typedef struct {
    uint32_t global_neuron_id;  // Source neuron global ID
    uint32_t timestamp_us;      // Spike timestamp
    uint8_t  flags;             // Spike flags
} z1_spike_event_t;

typedef struct {
    z1_spike_event_t events[Z1_SPIKE_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} z1_spike_queue_t;

// ============================================================================
// Function Prototypes
// ============================================================================

/**
 * Initialize SNN engine
 * 
 * @param node_id This node's ID
 * @return true if successful
 */
bool z1_snn_init(uint8_t node_id);

/**
 * Load neuron table from PSRAM
 * 
 * @param table_addr PSRAM address of neuron table
 * @param neuron_count Number of neurons
 * @return true if successful
 */
bool z1_snn_load_table(uint32_t table_addr, uint16_t neuron_count);

/**
 * Start SNN execution
 * 
 * @return true if successful
 */
bool z1_snn_start(void);

/**
 * Stop SNN execution
 */
void z1_snn_stop(void);

/**
 * Process one simulation timestep
 * 
 * @param timestep_us Timestep duration in microseconds
 */
void z1_snn_step(uint32_t timestep_us);

/**
 * Process incoming spike
 * 
 * @param global_neuron_id Source neuron global ID
 * @param timestamp_us Spike timestamp
 * @param flags Spike flags
 */
void z1_snn_process_spike(uint32_t global_neuron_id, uint32_t timestamp_us, uint8_t flags);

/**
 * Inject input spike (for input neurons)
 * 
 * @param local_neuron_id Local neuron ID
 * @param value Input value (0.0-1.0)
 */
void z1_snn_inject_input(uint16_t local_neuron_id, float value);

/**
 * Update synapse weight
 * 
 * @param local_neuron_id Local neuron ID
 * @param synapse_idx Synapse index
 * @param weight New weight (0-255)
 * @return true if successful
 */
bool z1_snn_update_weight(uint16_t local_neuron_id, uint8_t synapse_idx, uint8_t weight);

/**
 * Get SNN statistics
 * 
 * @param active_neurons Pointer to receive active neuron count
 * @param total_spikes Pointer to receive total spike count
 * @param spike_rate_hz Pointer to receive spike rate
 */
void z1_snn_get_stats(uint16_t* active_neurons, uint32_t* total_spikes, uint32_t* spike_rate_hz);

/**
 * Reset SNN state
 */
void z1_snn_reset(void);

// ============================================================================
// Neuron Processing Functions
// ============================================================================

/**
 * Load neuron from PSRAM
 * 
 * @param local_neuron_id Local neuron ID
 * @param neuron Pointer to receive neuron data
 * @return true if successful
 */
bool z1_neuron_load(uint16_t local_neuron_id, z1_neuron_entry_t* neuron);

/**
 * Save neuron to PSRAM
 * 
 * @param local_neuron_id Local neuron ID
 * @param neuron Pointer to neuron data
 * @return true if successful
 */
bool z1_neuron_save(uint16_t local_neuron_id, const z1_neuron_entry_t* neuron);

/**
 * Process neuron (integrate inputs and check for spike)
 * 
 * @param neuron Pointer to neuron data
 * @param timestep_us Timestep duration
 * @return true if neuron spiked
 */
bool z1_neuron_process(z1_neuron_entry_t* neuron, uint32_t timestep_us);

/**
 * Apply spike to neuron (update membrane potential)
 * 
 * @param neuron Pointer to neuron data
 * @param source_global_id Source neuron global ID
 * @param spike_flags Spike flags
 */
void z1_neuron_apply_spike(z1_neuron_entry_t* neuron, uint32_t source_global_id, uint8_t spike_flags);

/**
 * Generate spike (send to connected neurons)
 * 
 * @param neuron Pointer to neuron data
 */
void z1_neuron_generate_spike(const z1_neuron_entry_t* neuron);

// ============================================================================
// Synapse Helper Functions
// ============================================================================

/**
 * Pack synapse data
 * 
 * @param global_neuron_id Source neuron global ID
 * @param weight Weight (0-255)
 * @return Packed synapse value
 */
static inline z1_synapse_t z1_synapse_pack(uint32_t global_neuron_id, uint8_t weight) {
    return ((global_neuron_id & 0xFFFFFF) << 8) | weight;
}

/**
 * Unpack synapse global neuron ID
 * 
 * @param synapse Packed synapse value
 * @return Global neuron ID
 */
static inline uint32_t z1_synapse_get_id(z1_synapse_t synapse) {
    return (synapse >> 8) & 0xFFFFFF;
}

/**
 * Unpack synapse weight
 * 
 * @param synapse Packed synapse value
 * @return Weight (0-255)
 */
static inline uint8_t z1_synapse_get_weight(z1_synapse_t synapse) {
    return synapse & 0xFF;
}

/**
 * Set synapse weight
 * 
 * @param synapse Pointer to synapse
 * @param weight New weight (0-255)
 */
static inline void z1_synapse_set_weight(z1_synapse_t* synapse, uint8_t weight) {
    *synapse = (*synapse & 0xFFFFFF00) | weight;
}

// ============================================================================
// Spike Queue Functions
// ============================================================================

/**
 * Initialize spike queue
 * 
 * @param queue Pointer to queue
 */
void z1_spike_queue_init(z1_spike_queue_t* queue);

/**
 * Enqueue spike
 * 
 * @param queue Pointer to queue
 * @param spike Pointer to spike event
 * @return true if successful (false if queue full)
 */
bool z1_spike_queue_push(z1_spike_queue_t* queue, const z1_spike_event_t* spike);

/**
 * Dequeue spike
 * 
 * @param queue Pointer to queue
 * @param spike Pointer to receive spike event
 * @return true if successful (false if queue empty)
 */
bool z1_spike_queue_pop(z1_spike_queue_t* queue, z1_spike_event_t* spike);

/**
 * Check if queue is empty
 * 
 * @param queue Pointer to queue
 * @return true if empty
 */
static inline bool z1_spike_queue_empty(const z1_spike_queue_t* queue) {
    return queue->count == 0;
}

/**
 * Check if queue is full
 * 
 * @param queue Pointer to queue
 * @return true if full
 */
static inline bool z1_spike_queue_full(const z1_spike_queue_t* queue) {
    return queue->count >= Z1_SPIKE_QUEUE_SIZE;
}

// ============================================================================
// Global State
// ============================================================================

extern z1_snn_engine_state_t g_snn_state;
extern z1_spike_queue_t g_spike_queue;

// ============================================================================
// Compatibility Macros (header declares z1_snn_*, implementation has z1_snn_engine_*)
// ============================================================================

#define z1_snn_init(node_id)                z1_snn_engine_init(node_id)
#define z1_snn_load_table(addr, count)      z1_snn_engine_load_network(addr, count)
#define z1_snn_start()                      z1_snn_engine_start()
#define z1_snn_stop()                       z1_snn_engine_stop()
#define z1_snn_step(timestep)               z1_snn_engine_step(timestep)
#define z1_snn_inject_input(id, value)      z1_snn_engine_inject_spike(id, value)

#endif // Z1_SNN_ENGINE_H
