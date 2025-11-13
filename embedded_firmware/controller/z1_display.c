/**
 * Z1 Display Helper - SSD1306 OLED Status Updates
 * 
 * Provides display update functions for controller status
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_display.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

// I2C pins for OLED
#define OLED_I2C       i2c0
#define OLED_SDA_PIN   28
#define OLED_SCL_PIN   29

// Network configuration (extern from main)
extern const uint8_t IP_ADDRESS[4];

// Global state
static bool display_initialized = false;
static bool http_active = false;
static uint8_t active_nodes = 0;
static bool snn_running = false;
static uint32_t total_spikes = 0;

// Initialize display
bool z1_display_init(void) {
    printf("[Display] Initializing SSD1306 OLED...\n");
    
    // ssd1306_init returns void, so we just call it
    ssd1306_init(OLED_I2C, OLED_SDA_PIN, OLED_SCL_PIN);
    // Assume success (no error checking available)
    printf("[Display] ✅ Initialized\n");
    
    display_initialized = true;
    printf("[Display] ✅ Initialized\n");
    
    // Show boot message
    ssd1306_display_clear();
    ssd1306_write_line("Z1 SNN Controller", 0);
    ssd1306_draw_line(0, 10, 127, 10);
    ssd1306_write_line("Booting...", 3);
    ssd1306_display_update();
    
    return true;
}

// Helper to update full display
static void update_display(const char* status_line) {
    if (!display_initialized) {
        return;
    }
    
    ssd1306_display_clear();
    
    // Line 0: Title
    ssd1306_write_line("Z1 SNN Controller", 0);
    
    // Line 1: Separator
    ssd1306_draw_line(0, 10, 127, 10);
    
    // Line 2: IP Address
    char ip_str[22];
    snprintf(ip_str, sizeof(ip_str), "IP:%d.%d.%d.%d",
             IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    ssd1306_write_line(ip_str, 2);
    
    // Line 3: HTTP Status
    ssd1306_write_line(http_active ? "HTTP: Active" : "HTTP: Idle", 3);
    
    // Line 4: Node count
    char node_str[22];
    snprintf(node_str, sizeof(node_str), "Nodes: %d/16", active_nodes);
    ssd1306_write_line(node_str, 4);
    
    // Line 5: SNN Status
    if (snn_running) {
        char snn_str[22];
        snprintf(snn_str, sizeof(snn_str), "SNN: Running %luK", 
                 (unsigned long)(total_spikes / 1000));
        ssd1306_write_line(snn_str, 5);
    } else {
        ssd1306_write_line("SNN: Stopped", 5);
    }
    
    // Line 6-7: Status message (split if needed)
    if (strlen(status_line) > 21) {
        char line1[22], line2[22];
        strncpy(line1, status_line, 21);
        line1[21] = '\0';
        strncpy(line2, status_line + 21, 21);
        line2[21] = '\0';
        ssd1306_write_line(line1, 6);
        ssd1306_write_line(line2, 7);
    } else {
        ssd1306_write_line(status_line, 6);
    }
    
    ssd1306_display_update();
}

// Update display with status message
void z1_display_status(const char* message) {
    printf("[Display] %s\n", message);
    update_display(message);
}

// Update display with SNN deployment info
void z1_display_snn_deploy(uint32_t neuron_count, uint8_t node_count) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Deploy: %luN/%dNodes",
             (unsigned long)neuron_count, node_count);
    printf("[Display] SNN Deploy: %lu neurons, %d nodes\n",
           (unsigned long)neuron_count, node_count);
    update_display(msg);
}

// Update display with SNN execution status
void z1_display_snn_status(bool running, uint32_t spike_count) {
    snn_running = running;
    total_spikes = spike_count;
    
    char msg[64];
    if (running) {
        snprintf(msg, sizeof(msg), "SNN Running: %luK spikes",
                 (unsigned long)(spike_count / 1000));
    } else {
        snprintf(msg, sizeof(msg), "SNN Stopped: %luK total",
                 (unsigned long)(spike_count / 1000));
    }
    
    printf("[Display] SNN Status: %s, %lu spikes\n",
           running ? "Running" : "Stopped", (unsigned long)spike_count);
    update_display(msg);
}

// Update display with node discovery results
void z1_display_nodes(uint8_t active_count, uint8_t total_count) {
    active_nodes = active_count;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Discovered %d/%d nodes", active_count, total_count);
    printf("[Display] Nodes: %d/%d active\n", active_count, total_count);
    update_display(msg);
}

// Update display with HTTP request info
void z1_display_http_request(const char* method, const char* path) {
    http_active = true;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "%s %s", method, path);
    
    // Truncate path if too long
    if (strlen(msg) > 42) {
        msg[39] = '.';
        msg[40] = '.';
        msg[41] = '.';
        msg[42] = '\0';
    }
    
    printf("[Display] HTTP: %s %s\n", method, path);
    update_display(msg);
}

// Update display with error message
void z1_display_error(const char* error) {
    char msg[64];
    snprintf(msg, sizeof(msg), "ERROR: %s", error);
    printf("[Display] ERROR: %s\n", error);
    update_display(msg);
}
