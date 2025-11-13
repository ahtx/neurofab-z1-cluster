/**
 * Z1 Matrix Bus Protocol - Extended Commands Implementation
 * 
 * Implementation of extended protocol functions for controller
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_protocol_extended.h"
#include "z1_matrix_bus.h"
#include "z1_multiframe.h"
#include <string.h>
#include <stdio.h>

/**
 * Send command with multi-byte payload to a node
 * 
 * For now, this is a simplified implementation that sends the command
 * byte via the basic bus write. For multi-byte payloads, this should
 * be extended to use the multi-frame protocol.
 */
bool z1_bus_send_command(uint8_t target_node, uint8_t command, 
                         const uint8_t* data, uint16_t length) {
    // For simple commands (no data or single byte data)
    if (length == 0) {
        return z1_bus_write(target_node, command, 0);
    } else if (length == 1) {
        return z1_bus_write(target_node, command, data[0]);
    }
    
    // For multi-byte commands, use multi-frame protocol
    printf("[Z1 Protocol] Sending multi-byte command 0x%02X with %d bytes\n", 
           command, length);
    
    return z1_send_multiframe(target_node, command, data, length);
}

// z1_send_multiframe and z1_receive_multiframe are implemented in z1_multiframe.c

/**
 * Read memory from remote node (stub implementation)
 */
int z1_read_node_memory(uint8_t node_id, uint32_t address, 
                        uint8_t* buffer, uint16_t length) {
    // TODO: Implement memory read protocol
    printf("[Z1 Protocol] Memory read not yet implemented\n");
    return -1;
}

/**
 * Write memory to remote node (stub implementation)
 */
int z1_write_node_memory(uint8_t node_id, uint32_t address,
                         const uint8_t* data, uint16_t length) {
    // TODO: Implement memory write protocol
    printf("[Z1 Protocol] Memory write not yet implemented\n");
    return -1;
}

/**
 * Get node information (stub implementation)
 */
bool z1_get_node_info(uint8_t node_id, z1_node_info_t* info) {
    // TODO: Implement node info query
    return false;
}

/**
 * Execute code on remote node (stub implementation)
 */
int z1_execute_code(uint8_t node_id, uint32_t entry_point,
                    const uint32_t* params, uint8_t param_count) {
    // TODO: Implement remote code execution
    return -1;
}

/**
 * Reset remote node
 */
bool z1_reset_node(uint8_t node_id) {
    return z1_bus_write(node_id, Z1_CMD_RESET_NODE, 0);
}

/**
 * Send spike message
 */
bool z1_send_spike(const z1_spike_msg_t* spike) {
    if (!spike) return false;
    
    // Extract target node from global neuron ID
    uint8_t target_node = (spike->global_neuron_id >> 16) & 0xFF;
    
    // Pack spike data: [global_id:4][timestamp:4][flags:1]
    uint8_t spike_data[9];
    memcpy(&spike_data[0], &spike->global_neuron_id, 4);
    memcpy(&spike_data[4], &spike->timestamp_us, 4);
    spike_data[8] = spike->flags;
    
    printf("[Z1 Protocol] Sending spike (neuron %u) to node %d\n", 
           (unsigned int)spike->global_neuron_id, target_node);
    
    return z1_send_multiframe(target_node, Z1_CMD_SNN_SPIKE, spike_data, sizeof(spike_data));
}

/**
 * Update synapse weight (stub implementation)
 */
bool z1_update_weight(uint8_t node_id, const z1_weight_update_t* update) {
    // TODO: Implement weight update protocol
    return false;
}

/**
 * Start SNN execution on all nodes
 */
bool z1_start_snn_all(void) {
    return z1_bus_broadcast(Z1_CMD_SNN_START, 0);
}

/**
 * Stop SNN execution on all nodes
 */
bool z1_stop_snn_all(void) {
    return z1_bus_broadcast(Z1_CMD_SNN_STOP, 0);
}

/**
 * Query SNN activity from node (stub implementation)
 */
bool z1_query_snn_activity(uint8_t node_id, z1_snn_activity_t* activity) {
    // TODO: Implement activity query protocol
    return false;
}

// z1_discover_nodes_sequential and z1_bus_ping_node are implemented in z1_matrix_bus.c

// Duplicate functions removed - kept earlier implementations
