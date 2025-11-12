/**
 * Z1 Application Firmware Template
 * 
 * Template for creating custom firmware for Z1 compute nodes.
 * This firmware runs on top of the Z1 bootloader.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "../bootloader/z1_bootloader.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Application Configuration
// ============================================================================

#define APP_NAME            "Custom App"
#define APP_VERSION         "1.0.0"
#define APP_CAPABILITIES    FW_CAP_CUSTOM

// ============================================================================
// Global State
// ============================================================================

static uint8_t g_node_id = 0;
static z1_bootloader_api_t *g_bootloader_api = NULL;

// ============================================================================
// Application Functions
// ============================================================================

/**
 * Initialize application
 */
void app_init(void) {
    // Get bootloader API
    g_bootloader_api = GET_BOOTLOADER_API();
    
    // TODO: Initialize your application
    // - Set up peripherals
    // - Initialize data structures
    // - Configure PSRAM usage
}

/**
 * Main application loop
 */
void app_main(void) {
    while (1) {
        // TODO: Implement your application logic
        
        // Example: Echo messages received on Z1 bus
        uint8_t buffer[256];
        uint16_t len;
        
        if (g_bootloader_api->bus_receive(buffer, sizeof(buffer), &len)) {
            // Process received message
            // ...
            
            // Send response
            g_bootloader_api->bus_send(g_node_id, buffer, len);
        }
        
        // Example: Read/write PSRAM
        uint8_t data[128];
        g_bootloader_api->psram_read(APP_DATA_BASE, data, sizeof(data));
        
        // Process data
        // ...
        
        g_bootloader_api->psram_write(APP_DATA_BASE, data, sizeof(data));
        
        // Small delay
        uint32_t start = g_bootloader_api->get_system_time_ms();
        while (g_bootloader_api->get_system_time_ms() - start < 10) {
            // Wait 10ms
        }
    }
}

// ============================================================================
// Entry Point
// ============================================================================

/**
 * Application entry point
 * Called by bootloader after successful boot
 */
void __attribute__((section(".app_entry"))) app_entry(void) {
    // Initialize application
    app_init();
    
    // Run main loop
    app_main();
    
    // Should never return
    while(1);
}

// ============================================================================
// Vector Table
// ============================================================================

// Minimal vector table for application
// The bootloader sets VTOR to point here
__attribute__((section(".app_vectors")))
const void *app_vectors[] = {
    (void*)0x20080000,  // Initial stack pointer (end of SRAM)
    app_entry,          // Reset handler (entry point)
    // Add more handlers as needed
};
