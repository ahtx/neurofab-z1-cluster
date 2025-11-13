/**
 * Z1 Matrix Bus - Multi-Frame Protocol Header
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_MULTIFRAME_H
#define Z1_MULTIFRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "../common/z1_protocol.h"

// Send multi-frame message
bool z1_send_multiframe(uint8_t target_node, uint8_t command, 
                        const uint8_t* data, uint16_t length);

// Initialize receive buffer
bool z1_multiframe_rx_init(uint8_t* buffer, uint16_t buffer_size);

// Handle received frames
bool z1_multiframe_handle_start(uint8_t source_node, uint8_t command);
bool z1_multiframe_handle_length(uint8_t length_high, uint8_t length_low);
bool z1_multiframe_handle_data(uint8_t sequence, uint8_t byte1, uint8_t byte2);
bool z1_multiframe_handle_end(uint8_t checksum);

// Check receive status
bool z1_multiframe_rx_complete(void);
uint16_t z1_multiframe_rx_length(void);
void z1_multiframe_rx_reset(void);

#endif // Z1_MULTIFRAME_H
