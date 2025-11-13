/**
 * Z1 Cluster Controller - Main Entry Point
 * 
 * Initializes the controller node with HTTP API server for cluster management
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "z1_matrix_bus.h"
#include "z1_protocol_extended.h"
#include "z1_http_api.h"

// Controller is always node ID 16
#define Z1_CONTROLLER_NODE_ID  16

// LED pins (same as nodes)
#define LED_RED_PIN    44
#define LED_GREEN_PIN  45
#define LED_BLUE_PIN   46

/**
 * Initialize RGB LED with PWM
 */
static void init_led(void) {
    // Initialize GPIO pins for PWM
    gpio_set_function(LED_RED_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_BLUE_PIN, GPIO_FUNC_PWM);
    
    // Get PWM slices
    uint slice_r = pwm_gpio_to_slice_num(LED_RED_PIN);
    uint slice_g = pwm_gpio_to_slice_num(LED_GREEN_PIN);
    uint slice_b = pwm_gpio_to_slice_num(LED_BLUE_PIN);
    
    // Configure PWM
    pwm_set_wrap(slice_r, 255);
    pwm_set_wrap(slice_g, 255);
    pwm_set_wrap(slice_b, 255);
    
    // Set initial brightness (green for controller)
    pwm_set_gpio_level(LED_RED_PIN, 0);
    pwm_set_gpio_level(LED_GREEN_PIN, 50);
    pwm_set_gpio_level(LED_BLUE_PIN, 0);
    
    // Enable PWM
    pwm_set_enabled(slice_r, true);
    pwm_set_enabled(slice_g, true);
    pwm_set_enabled(slice_b, true);
}

/**
 * Set LED color
 */
static void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    pwm_set_gpio_level(LED_RED_PIN, r);
    pwm_set_gpio_level(LED_GREEN_PIN, g);
    pwm_set_gpio_level(LED_BLUE_PIN, b);
}

/**
 * Main entry point
 */
int main(void) {
    // Initialize standard I/O
    stdio_init_all();
    
    // Wait for USB serial to connect
    sleep_ms(2000);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Z1 Neuromorphic Cluster Controller v1.0                  ║\n");
    printf("║  NeuroFab Corp. - Distributed SNN Computing Platform       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Initialize LED
    printf("[Controller] Initializing RGB LED...\n");
    init_led();
    set_led_color(0, 100, 0);  // Green for initialization
    
    // Initialize matrix bus
    printf("[Controller] Initializing matrix bus (Node ID: %d)...\n", Z1_CONTROLLER_NODE_ID);
    if (!z1_bus_init(Z1_CONTROLLER_NODE_ID)) {
        printf("[Controller] ❌ Matrix bus initialization failed!\n");
        set_led_color(255, 0, 0);  // Red for error
        while (1) {
            sleep_ms(1000);
        }
    }
    printf("[Controller] ✅ Matrix bus initialized\n");
    
    // Set bus timing for fast parallel bus
    z1_bus_clock_high_us = 100;
    z1_bus_clock_low_us = 50;
    z1_bus_ack_timeout_ms = 500;
    z1_bus_backoff_base_us = 100;
    z1_bus_broadcast_hold_ms = 200;
    
    printf("[Controller] Bus timing configured (fast parallel bus)\n");
    printf("             High:%dus, Low:%dus, ACK:%dms, Backoff:%dus, Broadcast:%dms\n",
           (int)z1_bus_clock_high_us, (int)z1_bus_clock_low_us,
           (int)z1_bus_ack_timeout_ms, (int)z1_bus_backoff_base_us,
           (int)z1_bus_broadcast_hold_ms);
    
    // Discover nodes
    printf("[Controller] Discovering nodes in cluster...\n");
    bool active_nodes[16] = {false};
    z1_discover_nodes_sequential(active_nodes);
    
    int node_count = 0;
    for (int i = 0; i < 16; i++) {
        if (active_nodes[i]) {
            node_count++;
        }
    }
    printf("[Controller] ✅ Discovered %d active nodes\n", node_count);
    
    // TODO: Initialize HTTP server
    printf("[Controller] HTTP API server: Not yet implemented\n");
    printf("[Controller] To implement: lwIP + HTTP server on port 80\n");
    
    // Set LED to blue (ready)
    set_led_color(0, 0, 100);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Controller Ready                                          ║\n");
    printf("║  Nodes Active: %-3d                                         ║\n", node_count);
    printf("║  HTTP API: Not yet available                               ║\n");
    printf("║  Matrix Bus: Active                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Main loop
    uint32_t loop_count = 0;
    while (1) {
        // Heartbeat LED blink
        if (loop_count % 10 == 0) {
            set_led_color(0, 0, 255);  // Bright blue
        } else if (loop_count % 10 == 1) {
            set_led_color(0, 0, 50);   // Dim blue
        }
        
        // Process any incoming bus messages
        // (In a full implementation, this would be interrupt-driven)
        
        sleep_ms(100);
        loop_count++;
        
        // Periodic status
        if (loop_count % 100 == 0) {
            printf("[Controller] 💓 Heartbeat - Loop %d, Active nodes: %d\n", 
                   (int)loop_count, node_count);
        }
    }
    
    return 0;
}
