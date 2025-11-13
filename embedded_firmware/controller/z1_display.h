/**
 * Z1 Display Helper - SSD1306 OLED Status Updates
 * 
 * Provides display update functions for controller status
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_DISPLAY_H
#define Z1_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// Initialize display
bool z1_display_init(void);

// Update display with status message
void z1_display_status(const char* message);

// Update display with SNN deployment info
void z1_display_snn_deploy(uint32_t neuron_count, uint8_t node_count);

// Update display with SNN execution status
void z1_display_snn_status(bool running, uint32_t spike_count);

// Update display with node discovery results
void z1_display_nodes(uint8_t active_count, uint8_t total_count);

// Update display with HTTP request info
void z1_display_http_request(const char* method, const char* path);

// Update display with error message
void z1_display_error(const char* error);

#endif // Z1_DISPLAY_H
