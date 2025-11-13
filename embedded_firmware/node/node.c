/**
 * Z1 Neuromorphic Compute Node - Main Entry Point
 * 
 * Hardware: RP2350B with 16MB PSRAM
 * Function: Distributed spiking neural network compute node
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "z1_snn_engine.h"

// Configuration
#define SYSTEM_CLOCK_KHZ 150000  // 150 MHz (can overclock to 300 MHz)
#define NODE_ID_DEFAULT 0

// Function prototypes
static void init_hardware(void);
static void init_snn_engine(void);
static void main_loop(void);

/**
 * Main entry point
 */
int main(void) {
    // Initialize hardware
    init_hardware();
    
    printf("\n");
    printf("=====================================\n");
    printf("Z1 Neuromorphic Compute Node\n");
    printf("=====================================\n");
    printf("Hardware: RP2350B + 16MB PSRAM\n");
    printf("Clock: %d MHz\n", SYSTEM_CLOCK_KHZ / 1000);
    printf("Node ID: %d\n", NODE_ID_DEFAULT);
    printf("=====================================\n\n");
    
    // Initialize SNN engine
    init_snn_engine();
    
    // Main processing loop
    main_loop();
    
    return 0;
}

/**
 * Initialize hardware peripherals
 */
static void init_hardware(void) {
    // Set system clock
    set_sys_clock_khz(SYSTEM_CLOCK_KHZ, true);
    
    // Initialize stdio
    stdio_init_all();
    
    // Wait for USB connection (optional, with timeout)
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!stdio_usb_connected() && (to_ms_since_boot(get_absolute_time()) - start) < 2000) {
        sleep_ms(100);
    }
    
    // Initialize matrix bus (stub - to be implemented)
    // z1_matrix_bus_init();
    
    // Initialize PSRAM (stub - to be implemented)
    // psram_init();
    
    // Initialize OLED display (stub - to be implemented)
    // ssd1306_init();
}

/**
 * Initialize SNN engine
 */
static void init_snn_engine(void) {
    printf("Initializing SNN engine...\n");
    
    // Initialize the SNN engine
    // z1_snn_init();
    
    printf("SNN engine ready\n");
}

/**
 * Main processing loop
 */
static void main_loop(void) {
    printf("Entering main loop...\n");
    
    uint32_t loop_count = 0;
    
    while (1) {
        // Process incoming spikes from matrix bus
        // z1_matrix_bus_process();
        
        // Execute SNN timestep
        // z1_snn_step();
        
        // Send outgoing spikes to matrix bus
        // z1_matrix_bus_send_spikes();
        
        // Periodic status update
        if (loop_count % 10000 == 0) {
            printf(".");
            fflush(stdout);
        }
        
        loop_count++;
        
        // Small delay to prevent tight loop
        sleep_us(10);
    }
}
