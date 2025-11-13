/**
 * Z1 Matrix Bus Protocol - Unified Command Definitions
 * 
 * Single source of truth for all Z1 bus command codes.
 * Used by both controller and node firmware to ensure protocol consistency.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_PROTOCOL_H
#define Z1_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Basic Control Commands (0x10-0x1F)
// ============================================================================

#define Z1_CMD_GREEN_LED        0x10  // Green LED PWM control (0-255)
#define Z1_CMD_RED_LED          0x11  // Red LED PWM control (0-255)
#define Z1_CMD_BLUE_LED         0x12  // Blue LED PWM control (0-255)
#define Z1_CMD_STATUS           0x13  // Status request
#define Z1_CMD_PING             0x14  // Ping command
#define Z1_CMD_RESET_NODE       0x15  // Reset node
#define Z1_CMD_REBOOT           0x16  // Reboot node

// ============================================================================
// Firmware Management Commands (0x30-0x3F)
// ============================================================================

#define Z1_CMD_FIRMWARE_INFO        0x30  // Get firmware information
#define Z1_CMD_FIRMWARE_UPLOAD      0x31  // Upload firmware chunk
#define Z1_CMD_FIRMWARE_VERIFY      0x32  // Verify firmware checksum
#define Z1_CMD_FIRMWARE_INSTALL     0x33  // Install firmware
#define Z1_CMD_FIRMWARE_ACTIVATE    0x34  // Activate and reboot
#define Z1_CMD_BOOT_MODE            0x35  // Set boot mode

// ============================================================================
// Memory Access Commands (0x40-0x4F)
// ============================================================================

#define Z1_CMD_MEM_READ_REQ     0x40  // Request memory read
#define Z1_CMD_MEM_READ_DATA    0x41  // Memory read response
#define Z1_CMD_MEM_WRITE        0x42  // Write memory
#define Z1_CMD_MEM_WRITE_ACK    0x43  // Write acknowledgment
#define Z1_CMD_MEM_INFO         0x44  // Query memory info

// Aliases for compatibility
#define Z1_CMD_MEMORY_READ      Z1_CMD_MEM_READ_REQ
#define Z1_CMD_MEMORY_WRITE     Z1_CMD_MEM_WRITE

// ============================================================================
// Code Execution Commands (0x60-0x6F)
// ============================================================================

#define Z1_CMD_EXEC_CODE        0x60  // Execute code at address
#define Z1_CMD_EXEC_STATUS      0x61  // Query execution status
#define Z1_CMD_EXEC_RESULT      0x62  // Execution result

// ============================================================================
// SNN Engine Commands (0x70-0x7F)
// ============================================================================

#define Z1_CMD_SNN_SPIKE            0x70  // Neuron spike event (inter-node)
#define Z1_CMD_SNN_WEIGHT_UPDATE    0x71  // Update synapse weight
#define Z1_CMD_SNN_QUERY_ACTIVITY   0x72  // Query SNN activity
#define Z1_CMD_SNN_START            0x73  // Start SNN execution
#define Z1_CMD_SNN_STOP             0x74  // Stop SNN execution
#define Z1_CMD_SNN_RESET            0x75  // Reset SNN state
#define Z1_CMD_SNN_INPUT_SPIKE      0x76  // Inject external spike
#define Z1_CMD_SNN_GET_SPIKES       0x77  // Get output spikes
#define Z1_CMD_SNN_LOAD_TABLE       0x78  // Load neuron table from PSRAM
#define Z1_CMD_SNN_GET_STATUS       0x79  // Get SNN engine status

// Aliases for compatibility
#define Z1_CMD_SNN_INJECT_SPIKE Z1_CMD_SNN_INPUT_SPIKE

// ============================================================================
// Multi-Frame Protocol Commands (0xF0-0xFF)
// ============================================================================

#define Z1_CMD_FRAME_START      0xF0  // Start multi-frame transfer
#define Z1_CMD_FRAME_DATA       0xF1  // Frame data chunk
#define Z1_CMD_FRAME_END        0xF2  // End multi-frame transfer
#define Z1_CMD_FRAME_ACK        0xF3  // Frame acknowledgment
#define Z1_CMD_FRAME_NACK       0xF4  // Frame negative acknowledgment

// ============================================================================
// Protocol Constants
// ============================================================================

#define Z1_FRAME_HEADER         0xAA  // Frame header byte
#define Z1_BROADCAST_ID         31    // Broadcast address
#define Z1_CONTROLLER_ID        16    // Controller node ID
#define Z1_MAX_NODES            16    // Maximum regular nodes (0-15)
#define Z1_PING_DATA            0xA5  // Standard ping payload

// Multi-frame protocol constants
#define Z1_MAX_FRAME_SIZE       256   // Maximum bytes per frame
#define Z1_MAX_PAYLOAD_SIZE     254   // Max payload (256 - 2 header bytes)
#define Z1_MULTIFRAME_TIMEOUT_MS 1000 // Timeout for multi-frame transfer

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Multi-frame transfer state
 */
typedef struct {
    bool active;                    // Transfer in progress
    uint8_t target_node;            // Target node ID
    uint8_t sequence;               // Current sequence number
    uint16_t total_length;          // Total payload length
    uint16_t bytes_sent;            // Bytes sent so far
    uint32_t start_time_ms;         // Transfer start time
    const uint8_t* data;            // Pointer to data buffer
} z1_multiframe_tx_t;

/**
 * Multi-frame receive state
 */
typedef struct {
    bool active;                    // Receiving in progress
    uint8_t source_node;            // Source node ID
    uint8_t sequence;               // Expected sequence number
    uint16_t total_length;          // Total expected length
    uint16_t bytes_received;        // Bytes received so far
    uint32_t start_time_ms;         // Receive start time
    uint8_t* buffer;                // Receive buffer
    uint16_t buffer_size;           // Buffer capacity
} z1_multiframe_rx_t;

/**
 * Node information structure
 */
typedef struct {
    uint8_t node_id;
    uint8_t firmware_version_major;
    uint8_t firmware_version_minor;
    uint8_t firmware_version_patch;
    uint32_t uptime_ms;
    uint32_t free_memory;
    uint16_t neuron_count;
    bool snn_running;
} z1_node_info_t;

/**
 * Spike message structure
 */
typedef struct {
    uint32_t global_neuron_id;  // Bits [31:16] = node_id, [15:0] = local_id
    uint32_t timestamp_us;      // Spike timestamp
    uint8_t flags;              // Spike flags
} z1_spike_msg_t;

/**
 * Weight update structure
 */
typedef struct {
    uint16_t local_neuron_id;
    uint8_t synapse_idx;
    uint8_t new_weight;
} z1_weight_update_t;

/**
 * SNN activity query response
 */
typedef struct {
    uint16_t active_neurons;
    uint32_t total_spikes;
    uint32_t spike_rate_hz;
    uint32_t timestamp_us;
} z1_snn_activity_t;

#endif // Z1_PROTOCOL_H
