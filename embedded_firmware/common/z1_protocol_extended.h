/**
 * Z1 Matrix Bus Protocol - Extended Commands
 * 
 * Extended protocol definitions for memory operations, SNN coordination,
 * and cluster management.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_PROTOCOL_EXTENDED_H
#define Z1_PROTOCOL_EXTENDED_H

#include <stdint.h>
#include <stdbool.h>

// Include base protocol
#include "z1_matrix_bus.h"

// ============================================================================
// Extended Command Definitions
// ============================================================================

// Memory Operations (0x50-0x5F)
#define Z1_CMD_MEM_READ_REQ     0x50  // Request memory read
#define Z1_CMD_MEM_READ_DATA    0x51  // Memory read response
#define Z1_CMD_MEM_WRITE        0x52  // Write memory
#define Z1_CMD_MEM_WRITE_ACK    0x53  // Write acknowledgment
#define Z1_CMD_MEM_INFO         0x54  // Query memory info

// Bootloader / Firmware Management (0x30-0x3F)
#define Z1_CMD_FIRMWARE_INFO        0x30  // Get firmware information
#define Z1_CMD_FIRMWARE_UPLOAD      0x31  // Upload firmware to buffer
#define Z1_CMD_FIRMWARE_VERIFY      0x32  // Verify firmware in buffer
#define Z1_CMD_FIRMWARE_INSTALL     0x33  // Install firmware from buffer
#define Z1_CMD_FIRMWARE_ACTIVATE    0x34  // Activate new firmware and reboot
#define Z1_CMD_BOOT_MODE            0x35  // Set boot mode
#define Z1_CMD_REBOOT               0x36  // Reboot node

// Code Execution (0x60-0x6F)
#define Z1_CMD_EXEC_CODE        0x60  // Execute code at address
#define Z1_CMD_EXEC_STATUS      0x61  // Query execution status
#define Z1_CMD_EXEC_RESULT      0x62  // Execution result
#define Z1_CMD_RESET_NODE       0x63  // Reset node

// SNN Operations (0x70-0x7F)
#define Z1_CMD_SNN_SPIKE        0x70  // Neuron spike event
#define Z1_CMD_SNN_WEIGHT_UPDATE 0x71 // Update synapse weight
#define Z1_CMD_SNN_LOAD_TABLE   0x72  // Load neuron table
#define Z1_CMD_SNN_START        0x73  // Start SNN execution
#define Z1_CMD_SNN_STOP         0x74  // Stop SNN execution
#define Z1_CMD_SNN_QUERY_ACTIVITY 0x75 // Query spike count
#define Z1_CMD_SNN_ACTIVITY_RESP 0x76 // Activity response
#define Z1_CMD_SNN_INPUT_SPIKE  0x77  // Inject input spike

// Node Management (0x80-0x8F)
#define Z1_CMD_NODE_INFO_REQ    0x80  // Request node information
#define Z1_CMD_NODE_INFO_RESP   0x81  // Node information response
#define Z1_CMD_NODE_HEARTBEAT   0x82  // Heartbeat/keepalive
#define Z1_CMD_NODE_ERROR       0x83  // Error report

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Memory operation request
 */
typedef struct {
    uint32_t address;      // Memory address
    uint16_t length;       // Number of bytes
    uint8_t  flags;        // Operation flags
} z1_mem_request_t;

/**
 * Memory information response
 */
typedef struct {
    uint32_t total_memory;  // Total memory in bytes
    uint32_t free_memory;   // Free memory in bytes
    uint32_t psram_base;    // PSRAM base address
    uint32_t psram_size;    // PSRAM size
} z1_mem_info_t;

/**
 * Code execution request
 */
typedef struct {
    uint32_t entry_point;   // Entry point address
    uint8_t  param_count;   // Number of parameters
    uint32_t params[4];     // Up to 4 parameters
} z1_exec_request_t;

/**
 * Neuron spike message
 */
typedef struct {
    uint16_t local_neuron_id;  // Local neuron ID (0-65535)
    uint8_t  source_node_id;   // Source node ID
    uint8_t  flags;            // Spike flags
    uint32_t timestamp_us;     // Timestamp in microseconds
} z1_spike_msg_t;

/**
 * Weight update message
 */
typedef struct {
    uint16_t neuron_id;     // Target neuron ID
    uint8_t  synapse_idx;   // Synapse index
    uint8_t  weight;        // New weight value (0-255)
} z1_weight_update_t;

/**
 * Node information
 */
typedef struct {
    uint8_t  node_id;       // Node ID
    uint8_t  status;        // Status flags
    uint32_t uptime_ms;     // Uptime in milliseconds
    uint32_t free_memory;   // Free memory in bytes
    uint16_t neuron_count;  // Number of neurons loaded
    uint32_t spike_count;   // Total spikes processed
} z1_node_info_t;

/**
 * SNN activity query response
 */
typedef struct {
    uint16_t neuron_count;     // Number of neurons
    uint16_t active_neurons;   // Neurons that spiked
    uint32_t total_spikes;     // Total spikes
    uint32_t spike_rate_hz;    // Average spike rate
} z1_snn_activity_t;

// ============================================================================
// Protocol Constants
// ============================================================================

// Memory operation flags
#define Z1_MEM_FLAG_VERIFY      0x01  // Verify after write
#define Z1_MEM_FLAG_PSRAM       0x02  // Access PSRAM (vs Flash)
#define Z1_MEM_FLAG_CACHED      0x04  // Use cache if available

// Node status flags
#define Z1_NODE_STATUS_ACTIVE   0x01  // Node is active
#define Z1_NODE_STATUS_SNN_RUNNING 0x02 // SNN execution active
#define Z1_NODE_STATUS_ERROR    0x80  // Error condition

// Spike flags
#define Z1_SPIKE_FLAG_EXCITATORY 0x00 // Excitatory spike
#define Z1_SPIKE_FLAG_INHIBITORY 0x01 // Inhibitory spike

// Maximum transfer sizes
#define Z1_MAX_MEM_TRANSFER     256   // Max bytes per memory transfer
#define Z1_MAX_TABLE_TRANSFER   128   // Max bytes per table transfer

// ============================================================================
// Multi-Frame Transfer Protocol
// ============================================================================

/**
 * Multi-frame message header
 */
typedef struct {
    uint8_t  msg_type;      // Message type (command)
    uint8_t  frame_count;   // Total frames in message
    uint8_t  frame_idx;     // Current frame index (0-based)
    uint8_t  payload_len;   // Payload length in this frame
} z1_multiframe_header_t;

/**
 * Send multi-frame message
 * 
 * @param target_node Target node ID
 * @param msg_type Message type/command
 * @param data Data to send
 * @param length Data length
 * @return true if successful
 */
bool z1_send_multiframe(uint8_t target_node, uint8_t msg_type, 
                        const uint8_t* data, uint16_t length);

/**
 * Receive multi-frame message
 * 
 * @param buffer Buffer to receive data
 * @param max_length Maximum buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes received, or -1 on error
 */
int z1_receive_multiframe(uint8_t* buffer, uint16_t max_length, uint32_t timeout_ms);

// ============================================================================
// High-Level API Functions
// ============================================================================

/**
 * Read memory from remote node
 * 
 * @param node_id Target node ID
 * @param address Memory address
 * @param buffer Buffer to receive data
 * @param length Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int z1_read_node_memory(uint8_t node_id, uint32_t address, 
                        uint8_t* buffer, uint16_t length);

/**
 * Write memory to remote node
 * 
 * @param node_id Target node ID
 * @param address Memory address
 * @param data Data to write
 * @param length Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int z1_write_node_memory(uint8_t node_id, uint32_t address,
                         const uint8_t* data, uint16_t length);

/**
 * Get node information
 * 
 * @param node_id Target node ID
 * @param info Pointer to node info structure
 * @return true if successful
 */
bool z1_get_node_info(uint8_t node_id, z1_node_info_t* info);

/**
 * Execute code on remote node
 * 
 * @param node_id Target node ID
 * @param entry_point Entry point address
 * @param params Parameters array
 * @param param_count Number of parameters
 * @return Execution ID, or -1 on error
 */
int z1_execute_code(uint8_t node_id, uint32_t entry_point,
                    const uint32_t* params, uint8_t param_count);

/**
 * Reset remote node
 * 
 * @param node_id Target node ID
 * @return true if successful
 */
bool z1_reset_node(uint8_t node_id);

/**
 * Send spike message
 * 
 * @param spike Spike message structure
 * @return true if successful
 */
bool z1_send_spike(const z1_spike_msg_t* spike);

/**
 * Update synapse weight
 * 
 * @param node_id Target node ID
 * @param update Weight update structure
 * @return true if successful
 */
bool z1_update_weight(uint8_t node_id, const z1_weight_update_t* update);

/**
 * Start SNN execution on all nodes
 * 
 * @return true if successful
 */
bool z1_start_snn_all(void);

/**
 * Stop SNN execution on all nodes
 * 
 * @return true if successful
 */
bool z1_stop_snn_all(void);

/**
 * Query SNN activity from node
 * 
 * @param node_id Target node ID
 * @param activity Pointer to activity structure
 * @return true if successful
 */
bool z1_query_snn_activity(uint8_t node_id, z1_snn_activity_t* activity);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Encode global neuron ID
 * 
 * @param node_id Node ID (0-255)
 * @param local_neuron_id Local neuron ID (0-65535)
 * @return 24-bit global neuron ID
 */
static inline uint32_t z1_encode_neuron_id(uint8_t node_id, uint16_t local_neuron_id) {
    return ((uint32_t)node_id << 16) | local_neuron_id;
}

/**
 * Decode global neuron ID
 * 
 * @param global_id Global neuron ID
 * @param node_id Pointer to receive node ID
 * @param local_neuron_id Pointer to receive local neuron ID
 */
static inline void z1_decode_neuron_id(uint32_t global_id, 
                                       uint8_t* node_id, 
                                       uint16_t* local_neuron_id) {
    *node_id = (global_id >> 16) & 0xFF;
    *local_neuron_id = global_id & 0xFFFF;
}

#endif // Z1_PROTOCOL_EXTENDED_H
