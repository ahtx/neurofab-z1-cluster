/**
 * Z1 Matrix Bus Protocol - Extended Functions
 * 
 * Extended protocol helper functions for controller firmware.
 * All command and type definitions are in z1_protocol.h
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_PROTOCOL_EXTENDED_H
#define Z1_PROTOCOL_EXTENDED_H

#include <stdint.h>
#include <stdbool.h>

// Include unified protocol (has all commands and types)
#include "z1_protocol.h"

// ============================================================================
// Extended Protocol Functions (Controller-Side)
// ============================================================================

/**
 * Send spike to target node
 */
bool z1_send_spike(z1_spike_msg_t* spike_msg);

/**
 * Get node information
 */
bool z1_get_node_info(uint8_t node_id, z1_node_info_t* info);

/**
 * Get SNN activity from node
 */
bool z1_get_snn_activity(uint8_t node_id, z1_snn_activity_t* activity);

/**
 * Send multi-byte command with data
 */
bool z1_bus_send_command(uint8_t target_id, uint8_t command, const uint8_t* data, uint16_t length);

#endif // Z1_PROTOCOL_EXTENDED_H
