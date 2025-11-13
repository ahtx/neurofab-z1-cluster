/**
 * Z1 Cluster Controller - Main Entry Point
 * 
 * Complete neuromorphic cluster controller with:
 * - W5500 Ethernet HTTP server
 * - SNN API endpoints
 * - SSD1306 OLED display
 * - Matrix bus cluster management
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// Project headers
#include "w5500_http_server.h"
#include "z1_matrix_bus.h"
#include "z1_protocol_extended.h"
#include "z1_display.h"
#include "psram_rp2350.h"

// Controller is always node ID 16
#define Z1_CONTROLLER_NODE_ID  16

// LED pins
#define LED_RED_PIN    44
#define LED_GREEN_PIN  45
#define LED_BLUE_PIN   46

// ============================================================================
// LED Control
// ============================================================================

static void init_led(void) {
    gpio_set_function(LED_RED_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_BLUE_PIN, GPIO_FUNC_PWM);
    
    uint slice_r = pwm_gpio_to_slice_num(LED_RED_PIN);
    uint slice_g = pwm_gpio_to_slice_num(LED_GREEN_PIN);
    uint slice_b = pwm_gpio_to_slice_num(LED_BLUE_PIN);
    
    pwm_set_wrap(slice_r, 255);
    pwm_set_wrap(slice_g, 255);
    pwm_set_wrap(slice_b, 255);
    
    pwm_set_gpio_level(LED_RED_PIN, 0);
    pwm_set_gpio_level(LED_GREEN_PIN, 0);
    pwm_set_gpio_level(LED_BLUE_PIN, 0);
    
    pwm_set_enabled(slice_r, true);
    pwm_set_enabled(slice_g, true);
    pwm_set_enabled(slice_b, true);
}

static void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    pwm_set_gpio_level(LED_RED_PIN, r);
    pwm_set_gpio_level(LED_GREEN_PIN, g);
    pwm_set_gpio_level(LED_BLUE_PIN, b);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Z1 Neuromorphic Cluster Controller v1.0                  ║\n");
    printf("║  W5500 HTTP Server + SSD1306 Display + Matrix Bus         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Initialize LED
    printf("[Init] RGB LED...\n");
    init_led();
    set_led_color(100, 100, 0);  // Yellow = booting
    
    // Initialize OLED Display
    printf("[Init] OLED Display...\n");
    if (!z1_display_init()) {
        printf("[Init] ⚠️  OLED init failed, continuing without display\n");
    }
    
    // Initialize PSRAM
    printf("[Init] PSRAM...\n");
    z1_display_status("Init PSRAM...");
    if (!psram_init()) {
        printf("[Init] ⚠️  PSRAM init failed\n");
        z1_display_status("PSRAM failed!");
    } else {
        printf("[Init] ✅ PSRAM OK\n");
    }
    
    // Initialize Matrix Bus
    printf("[Init] Matrix Bus (Controller ID: %d)...\n", Z1_CONTROLLER_NODE_ID);
    z1_display_status("Init bus...");
    if (!z1_bus_init(Z1_CONTROLLER_NODE_ID)) {
        printf("[Init] ❌ Bus init failed!\n");
        z1_display_error("Bus init failed!");
        set_led_color(255, 0, 0);  // Red = error
        while (1) sleep_ms(1000);
    }
    printf("[Init] ✅ Matrix bus OK\n");
    
    // Discover nodes
    printf("[Init] Discovering nodes...\n");
    z1_display_status("Scanning nodes...");
    bool active_nodes[16] = {false};
    z1_discover_nodes_sequential(active_nodes);
    
    uint8_t active_node_count = 0;
    for (int i = 0; i < 16; i++) {
        if (active_nodes[i]) {
            active_node_count++;
            printf("[Init]   Node %d: ACTIVE\n", i);
        }
    }
    printf("[Init] ✅ Found %d nodes\n", active_node_count);
    z1_display_nodes(active_node_count, 16);
    sleep_ms(1000);
    
    // Initialize W5500 Ethernet
    printf("[Init] W5500 Ethernet...\n");
    if (!w5500_init()) {
        printf("[Init] ❌ W5500 init failed!\n");
        z1_display_error("W5500 init failed!");
        set_led_color(255, 0, 0);  // Red = error
        while (1) sleep_ms(1000);
    }
    
    // Setup TCP server
    printf("[Init] HTTP Server...\n");
    if (!w5500_setup_tcp_server(80)) {
        printf("[Init] ❌ TCP server failed!\n");
        z1_display_error("TCP server failed!");
        set_led_color(255, 0, 0);  // Red = error
        while (1) sleep_ms(1000);
    }
    
    set_led_color(0, 0, 255);  // Blue = ready
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  ✅ Controller Ready                                       ║\n");
    printf("║                                                            ║\n");
    printf("║  HTTP Server: http://192.168.1.222                        ║\n");
    printf("║  Nodes Active: %-3d                                         ║\n", active_node_count);
    printf("║                                                            ║\n");
    printf("║  API Endpoints:                                            ║\n");
    printf("║    GET  /api/nodes                                         ║\n");
    printf("║    POST /api/nodes/discover                                ║\n");
    printf("║    GET  /api/snn/status                                    ║\n");
    printf("║    POST /api/snn/deploy                                    ║\n");
    printf("║    POST /api/snn/start                                     ║\n");
    printf("║    POST /api/snn/stop                                      ║\n");
    printf("║    POST /api/snn/input                                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    z1_display_status("HTTP listening");
    
    // Run HTTP server (blocking loop)
    w5500_http_server_run();
    
    return 0;
}
