/**
 * Hardware Configuration
 * 
 * Board-specific hardware configuration and initialization
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

// LED pin
#define LED_PIN 25

// Hardware configuration
typedef struct {
    uint8_t node_id;
    uint8_t backplane_id;
    bool has_psram;
    bool has_display;
} hw_config_t;

static hw_config_t hw_config = {
    .node_id = 0,
    .backplane_id = 0,
    .has_psram = true,
    .has_display = true
};

/**
 * Initialize hardware configuration
 */
void hw_config_init(void) {
    printf("Initializing hardware configuration...\n");
    
    // Initialize LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
    
    // Read node ID from GPIO pins or DIP switches
    // TODO: Implement node ID detection
    
    printf("Hardware configuration:\n");
    printf("  Node ID: %d\n", hw_config.node_id);
    printf("  Backplane ID: %d\n", hw_config.backplane_id);
    printf("  PSRAM: %s\n", hw_config.has_psram ? "Yes" : "No");
    printf("  Display: %s\n", hw_config.has_display ? "Yes" : "No");
}

/**
 * Get node ID
 */
uint8_t hw_config_get_node_id(void) {
    return hw_config.node_id;
}

/**
 * Get backplane ID
 */
uint8_t hw_config_get_backplane_id(void) {
    return hw_config.backplane_id;
}

/**
 * Set LED state
 */
void hw_config_set_led(bool state) {
    gpio_put(LED_PIN, state);
}

/**
 * Toggle LED
 */
void hw_config_toggle_led(void) {
    gpio_put(LED_PIN, !gpio_get(LED_PIN));
}

/**
 * Check if PSRAM is available
 */
bool hw_config_has_psram(void) {
    return hw_config.has_psram;
}

/**
 * Check if display is available
 */
bool hw_config_has_display(void) {
    return hw_config.has_display;
}
