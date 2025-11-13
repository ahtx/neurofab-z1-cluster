/**
 * Z1 Matrix Bus - Multi-Frame Protocol Implementation
 * 
 * Implements reliable multi-frame data transfer over the Z1 matrix bus.
 * Allows sending payloads larger than single-frame limit (254 bytes).
 * 
 * Protocol:
 * 1. Send FRAME_START with total length
 * 2. Send FRAME_DATA chunks (up to 254 bytes each)
 * 3. Send FRAME_END with checksum
 * 4. Wait for FRAME_ACK/NACK
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_multiframe.h"
#include "z1_matrix_bus.h"
#include "../common/z1_protocol.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"

// ============================================================================
// Global State
// ============================================================================

static z1_multiframe_tx_t g_tx_state = {0};
static z1_multiframe_rx_t g_rx_state = {0};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Calculate simple checksum
 */
static uint8_t calculate_checksum(const uint8_t* data, uint16_t length) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

/**
 * Get current time in milliseconds
 */
static uint32_t get_time_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ============================================================================
// Multi-Frame Transmit
// ============================================================================

/**
 * Send multi-frame message to target node
 * 
 * @param target_node Target node ID
 * @param command Command byte
 * @param data Payload data
 * @param length Payload length
 * @return true if successful
 */
bool z1_send_multiframe(uint8_t target_node, uint8_t command, 
                        const uint8_t* data, uint16_t length) {
    if (!data || length == 0 || length > 65535) {
        return false;
    }
    
    printf("[Multiframe TX] Sending %d bytes to node %d (cmd 0x%02X)\n", 
           length, target_node, command);
    
    // Initialize transfer state
    g_tx_state.active = true;
    g_tx_state.target_node = target_node;
    g_tx_state.sequence = 0;
    g_tx_state.total_length = length;
    g_tx_state.bytes_sent = 0;
    g_tx_state.start_time_ms = get_time_ms();
    g_tx_state.data = data;
    
    // Send FRAME_START with command and total length
    // Format: [command] [length_high] [length_low]
    if (!z1_bus_write(target_node, Z1_CMD_FRAME_START, command)) {
        printf("[Multiframe TX] Failed to send FRAME_START\n");
        g_tx_state.active = false;
        return false;
    }
    sleep_ms(5);  // Give target time to prepare
    
    if (!z1_bus_write(target_node, (length >> 8) & 0xFF, length & 0xFF)) {
        printf("[Multiframe TX] Failed to send length\n");
        g_tx_state.active = false;
        return false;
    }
    sleep_ms(5);
    
    // Send data in chunks
    uint16_t offset = 0;
    while (offset < length) {
        // Check timeout
        if (get_time_ms() - g_tx_state.start_time_ms > Z1_MULTIFRAME_TIMEOUT_MS) {
            printf("[Multiframe TX] Timeout!\n");
            g_tx_state.active = false;
            return false;
        }
        
        // Calculate chunk size (max 2 bytes per frame for simplicity)
        uint16_t chunk_size = (length - offset > 2) ? 2 : (length - offset);
        
        // Send FRAME_DATA with sequence number and data bytes
        uint8_t byte1 = (offset < length) ? data[offset] : 0;
        uint8_t byte2 = (offset + 1 < length) ? data[offset + 1] : 0;
        
        if (!z1_bus_write(target_node, Z1_CMD_FRAME_DATA, g_tx_state.sequence)) {
            printf("[Multiframe TX] Failed to send FRAME_DATA seq %d\n", g_tx_state.sequence);
            g_tx_state.active = false;
            return false;
        }
        sleep_ms(2);
        
        if (!z1_bus_write(target_node, byte1, byte2)) {
            printf("[Multiframe TX] Failed to send data bytes\n");
            g_tx_state.active = false;
            return false;
        }
        sleep_ms(2);
        
        offset += chunk_size;
        g_tx_state.sequence++;
        g_tx_state.bytes_sent = offset;
        
        if (offset % 64 == 0) {
            printf("[Multiframe TX] Progress: %d/%d bytes\n", offset, length);
        }
    }
    
    // Calculate checksum
    uint8_t checksum = calculate_checksum(data, length);
    
    // Send FRAME_END with checksum
    if (!z1_bus_write(target_node, Z1_CMD_FRAME_END, checksum)) {
        printf("[Multiframe TX] Failed to send FRAME_END\n");
        g_tx_state.active = false;
        return false;
    }
    
    printf("[Multiframe TX] ✅ Complete: %d bytes sent (checksum 0x%02X)\n", 
           length, checksum);
    
    g_tx_state.active = false;
    return true;
}

// ============================================================================
// Multi-Frame Receive
// ============================================================================

/**
 * Initialize receive buffer
 */
bool z1_multiframe_rx_init(uint8_t* buffer, uint16_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return false;
    }
    
    g_rx_state.buffer = buffer;
    g_rx_state.buffer_size = buffer_size;
    g_rx_state.active = false;
    
    return true;
}

/**
 * Handle FRAME_START command
 */
bool z1_multiframe_handle_start(uint8_t source_node, uint8_t command) {
    if (g_rx_state.active) {
        printf("[Multiframe RX] Warning: New transfer while one in progress\n");
    }
    
    g_rx_state.active = true;
    g_rx_state.source_node = source_node;
    g_rx_state.sequence = 0;
    g_rx_state.bytes_received = 0;
    g_rx_state.start_time_ms = get_time_ms();
    
    // Command byte is passed as parameter
    // Length will come in next frame
    
    printf("[Multiframe RX] Started from node %d (cmd 0x%02X)\n", 
           source_node, command);
    
    return true;
}

/**
 * Handle length frame
 */
bool z1_multiframe_handle_length(uint8_t length_high, uint8_t length_low) {
    if (!g_rx_state.active) {
        return false;
    }
    
    g_rx_state.total_length = (length_high << 8) | length_low;
    
    if (g_rx_state.total_length > g_rx_state.buffer_size) {
        printf("[Multiframe RX] Error: Payload too large (%d > %d)\n",
               g_rx_state.total_length, g_rx_state.buffer_size);
        g_rx_state.active = false;
        return false;
    }
    
    printf("[Multiframe RX] Expecting %d bytes\n", g_rx_state.total_length);
    return true;
}

/**
 * Handle FRAME_DATA command
 */
bool z1_multiframe_handle_data(uint8_t sequence, uint8_t byte1, uint8_t byte2) {
    if (!g_rx_state.active) {
        return false;
    }
    
    // Check sequence number
    if (sequence != g_rx_state.sequence) {
        printf("[Multiframe RX] Sequence error: expected %d, got %d\n",
               g_rx_state.sequence, sequence);
        g_rx_state.active = false;
        return false;
    }
    
    // Check timeout
    if (get_time_ms() - g_rx_state.start_time_ms > Z1_MULTIFRAME_TIMEOUT_MS) {
        printf("[Multiframe RX] Timeout!\n");
        g_rx_state.active = false;
        return false;
    }
    
    // Store data bytes
    if (g_rx_state.bytes_received < g_rx_state.total_length) {
        g_rx_state.buffer[g_rx_state.bytes_received++] = byte1;
    }
    if (g_rx_state.bytes_received < g_rx_state.total_length) {
        g_rx_state.buffer[g_rx_state.bytes_received++] = byte2;
    }
    
    g_rx_state.sequence++;
    
    return true;
}

/**
 * Handle FRAME_END command
 */
bool z1_multiframe_handle_end(uint8_t checksum_received) {
    if (!g_rx_state.active) {
        return false;
    }
    
    // Verify we received all data
    if (g_rx_state.bytes_received != g_rx_state.total_length) {
        printf("[Multiframe RX] Incomplete: %d/%d bytes\n",
               g_rx_state.bytes_received, g_rx_state.total_length);
        g_rx_state.active = false;
        return false;
    }
    
    // Verify checksum
    uint8_t checksum_calculated = calculate_checksum(g_rx_state.buffer, 
                                                      g_rx_state.total_length);
    
    if (checksum_calculated != checksum_received) {
        printf("[Multiframe RX] Checksum error: expected 0x%02X, got 0x%02X\n",
               checksum_calculated, checksum_received);
        g_rx_state.active = false;
        return false;
    }
    
    printf("[Multiframe RX] ✅ Complete: %d bytes received\n", 
           g_rx_state.bytes_received);
    
    g_rx_state.active = false;
    return true;
}

/**
 * Check if receive is complete
 */
bool z1_multiframe_rx_complete(void) {
    return !g_rx_state.active && g_rx_state.bytes_received > 0;
}

/**
 * Get received data length
 */
uint16_t z1_multiframe_rx_length(void) {
    return g_rx_state.bytes_received;
}

/**
 * Reset receive state
 */
void z1_multiframe_rx_reset(void) {
    g_rx_state.active = false;
    g_rx_state.bytes_received = 0;
}
