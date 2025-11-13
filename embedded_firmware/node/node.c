#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "hardware/regs/io_bank0.h"
#include "hardware/regs/sio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"

// Include the Z1 Matrix Bus library
#include "z1_matrix_bus.h"

// LED Pin Definitions for nodes (PWM capable pins)
#define LED_GREEN      44
#define LED_BLUE       45  
#define LED_RED        46

// LED state tracking
static uint8_t led_red_pwm = 0;
static uint8_t led_green_pwm = 0;
static uint8_t led_blue_pwm = 0;

// Ping response state tracking
static bool ping_response_pending = false;
static uint8_t ping_response_target = 0;
static uint8_t ping_response_data = 0;

// PWM slice numbers for LEDs
static uint red_slice, green_slice, blue_slice;

// Define GPIO pins
uint8_t NODEID_BASE = 40; //40-43 used for node ID
uint8_t NUM_PINS = 4;
uint8_t Z1_NODE_ID = 0; // Will be set in main()

bool raw_pad_value(uint8_t pin) {
    return (io_bank0_hw->io[pin].status >> 26) & 0x1;  // Bit 26 = IN
}

void force_disable_pulls(uint pin) {
    pads_bank0_hw->io[pin] &= ~(1 << 2);  // Clear PUE (bit 2)
    pads_bank0_hw->io[pin] &= ~(1 << 1);  // Clear PDE (bit 1)
}

// Initialize GPIOs as inputs with no pull-up/down
void init_gpio_inputs() {
    for (uint8_t i = 0; i < NUM_PINS; ++i) {
        uint8_t pin = NODEID_BASE + i;
        gpio_init(pin);
        gpio_set_function(pin, GPIO_FUNC_SIO);  // Ensure pin is in GPIO mode
        gpio_set_dir(pin, GPIO_IN);
        force_disable_pulls(pin);
    }
}

// Read GPIO40–43 and return value (0–15)  
// gpio40-43 DO NOT read properly with gpio_get due to SDK issue, I think
uint8_t read_NODEID() {
    uint8_t result = 0;
    for (uint8_t i = 0; i < NUM_PINS; ++i) {
        uint8_t pin = NODEID_BASE + i;      
        result |= (raw_pad_value(pin) << i);  // GPIO40 is bit 0, GPIO43 is bit 3
    }
    return result;
}

// Function to initialize local LEDs with PWM
void init_local_leds(void) {
    printf("Node %d: Initializing PWM LEDs (R:%d G:%d B:%d)\n", 
           Z1_NODE_ID, LED_RED, LED_GREEN, LED_BLUE);
    
    // Initialize GPIO pins for PWM
    gpio_set_function(LED_RED, GPIO_FUNC_PWM);
    gpio_set_function(LED_GREEN, GPIO_FUNC_PWM);
    gpio_set_function(LED_BLUE, GPIO_FUNC_PWM);
    
    // Get PWM slices
    red_slice = pwm_gpio_to_slice_num(LED_RED);
    green_slice = pwm_gpio_to_slice_num(LED_GREEN);
    blue_slice = pwm_gpio_to_slice_num(LED_BLUE);
    
    // Set PWM frequency (1kHz)
    pwm_set_clkdiv(red_slice, 125.0f);    // 125MHz / 125 = 1MHz
    pwm_set_wrap(red_slice, 999);         // 1MHz / 1000 = 1kHz
    pwm_set_clkdiv(green_slice, 125.0f);
    pwm_set_wrap(green_slice, 999);
    pwm_set_clkdiv(blue_slice, 125.0f);
    pwm_set_wrap(blue_slice, 999);
    
    // Start with LEDs off
    pwm_set_gpio_level(LED_RED, 0);
    pwm_set_gpio_level(LED_GREEN, 0);
    pwm_set_gpio_level(LED_BLUE, 0);
    
    // Enable PWM
    pwm_set_enabled(red_slice, true);
    pwm_set_enabled(green_slice, true);
    pwm_set_enabled(blue_slice, true);
    
    printf("Node %d: ✅ PWM LEDs initialized\n", Z1_NODE_ID);
}

// Function to set LED PWM value (0-255 input, scaled to PWM range)
void set_led_pwm(uint8_t led_pin, uint8_t pwm_value) {
    // Scale 0-255 to 0-999 (PWM wrap value)
    uint16_t pwm_level = (pwm_value * 999) / 255;
    
    pwm_set_gpio_level(led_pin, pwm_level);
    
    printf("Node %d: LED pin %d set to PWM %d/255 (level %d/999)\n", 
           Z1_NODE_ID, led_pin, pwm_value, pwm_level);
}

// Process bus commands and update LEDs
void process_bus_command(uint8_t command, uint8_t data) {
    printf("\n[Node %d] *** PROCESSING BUS COMMAND ***\n", Z1_NODE_ID);
    printf("[Node %d] Command: 0x%02X, Data: %d, Sender: node %d\n", 
           Z1_NODE_ID, command, data, z1_last_sender_id);
    
    switch (command) {
        case Z1_CMD_GREEN_LED:
            led_green_pwm = data;
            set_led_pwm(LED_GREEN, led_green_pwm);
            printf("[Node %d] ✅ GREEN LED PWM updated: %d/255 (from node %d)\n", 
                   Z1_NODE_ID, data, z1_last_sender_id);
            break;
            
        case Z1_CMD_RED_LED:
            led_red_pwm = data;
            set_led_pwm(LED_RED, led_red_pwm);
            printf("[Node %d] ✅ RED LED PWM updated: %d/255 (from node %d)\n", 
                   Z1_NODE_ID, data, z1_last_sender_id);
            break;
            
        case Z1_CMD_BLUE_LED:
            led_blue_pwm = data;
            set_led_pwm(LED_BLUE, led_blue_pwm);
            printf("[Node %d] ✅ BLUE LED PWM updated: %d/255 (from node %d)\n", 
                   Z1_NODE_ID, data, z1_last_sender_id);
            break;
            
        case Z1_CMD_STATUS:
            printf("[Node %d] 📊 STATUS response to node %d - R:%d G:%d B:%d\n", 
                   Z1_NODE_ID, z1_last_sender_id, led_red_pwm, led_green_pwm, led_blue_pwm);
            break;
            
        case Z1_CMD_PING:
            printf("[Node %d] 🏓 PING received from node %d with data 0x%02X\n", 
                   Z1_NODE_ID, z1_last_sender_id, data);
            // DEFER response - don't call z1_bus_write from interrupt context!
            printf("[Node %d] 🏓 Deferring PING RESPONSE to node %d\n", Z1_NODE_ID, z1_last_sender_id);
            ping_response_pending = true;
            ping_response_target = z1_last_sender_id;
            ping_response_data = data;
            break;
            
        default:
            printf("[Node %d] ❓ UNKNOWN command 0x%02X from node %d\n", 
                   Z1_NODE_ID, command, z1_last_sender_id);
            break;
    }
    
    printf("[Node %d] *** COMMAND PROCESSING COMPLETE ***\n\n", Z1_NODE_ID);
}

// Callback function called by bus interrupt handler when commands are received
void z1_bus_process_command(uint8_t command, uint8_t data) {
    process_bus_command(command, data);
}

// Main loop for node operation
int main() {
    // Initialize serial console first
    stdio_init_all();
    init_gpio_inputs(); // Initialize GPIO40-43 as inputs
    Z1_NODE_ID = read_NODEID();     
    // Initialize random seed based on node ID and boot time for collision avoidance
    srand(Z1_NODE_ID * 1000 + (uint32_t)time_us_32());

    sleep_ms(500);  // Allow time for stdio to initialize
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              Z1 Matrix Computer Node %02d Starting             ║\n", Z1_NODE_ID);
    printf("║                    Verbose Logging Enabled                   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Configure bus timing (tunable parameters)
    z1_bus_clock_high_us = 100;   // Clock high time between frames
    z1_bus_clock_low_us = 50;     // Clock low time for data latch
    z1_bus_ack_timeout_ms = 10;   // ACK wait timeout (fast bus)
    z1_bus_backoff_base_us = 50;  // Base backoff for collisions (reduced)
    z1_bus_broadcast_hold_ms = 10; // Broadcast hold time (reduced)
    
    printf("Node %d: Bus timing configured (fast parallel bus) - High:%dus, Low:%dus, ACK:%dms, Backoff:%dus, Broadcast:%dms\n",
           Z1_NODE_ID, z1_bus_clock_high_us, z1_bus_clock_low_us, 
           z1_bus_ack_timeout_ms, z1_bus_backoff_base_us, z1_bus_broadcast_hold_ms);
    
    // Initialize local hardware
    init_local_leds();
    
    // Initialize the Z1 Matrix Bus
    printf("Node %d: Initializing Z1 Matrix Bus...\n", Z1_NODE_ID);
    if (!z1_bus_init(Z1_NODE_ID)) {
        printf("Node %d: ❌ FATAL: Failed to initialize Z1 Matrix Bus\n", Z1_NODE_ID);
        return -1;
    }
    
    printf("Node %d: ✅ Bus initialization complete\n", Z1_NODE_ID);
    printf("Node %d: 🔄 Entering main loop - listening for bus transactions\n", Z1_NODE_ID);
    
    // Startup LED sequence to show we're alive
    printf("Node %d: 🎨 Running startup LED sequence\n", Z1_NODE_ID);
    set_led_pwm(LED_RED, 128);
    sleep_ms(300);
    set_led_pwm(LED_RED, 0);
    set_led_pwm(LED_GREEN, 128);
    sleep_ms(300);
    set_led_pwm(LED_GREEN, 0);
    set_led_pwm(LED_BLUE, 128);
    sleep_ms(300);
    set_led_pwm(LED_BLUE, 0);
    
    printf("Node %d: 🚀 Ready for bus operations!\n", Z1_NODE_ID);


    // Main loop - bus operations handled by interrupt
    uint32_t loop_count = 0;
    while (true) {
        // Periodic status update every 10 seconds
        if (loop_count % 1000 == 0) {
            printf("Node %d: 💓 Heartbeat - Loop %d, Active:%s, R:%d G:%d B:%d\n", 
                   Z1_NODE_ID, loop_count, 
                   z1_bus_transaction_active ? "YES" : "NO",
                   led_red_pwm, led_green_pwm, led_blue_pwm);
        }
        
        // Call bus handler (mostly handled by interrupts now)
        z1_bus_handle_interrupt();
        
        // Handle deferred ping responses (outside interrupt context)
        if (ping_response_pending) {
            // Respond immediately - no artificial delay needed for fast parallel bus
            printf("[Node %d] 🏓 Sending PING RESPONSE to node %d with data 0x%02X\n", 
                   Z1_NODE_ID, ping_response_target, ping_response_data);
            
            if (z1_bus_write(ping_response_target, Z1_CMD_PING, ping_response_data)) {
                printf("[Node %d] ✅ PING RESPONSE sent successfully\n", Z1_NODE_ID);
            } else {
                printf("[Node %d] ❌ PING RESPONSE failed to send\n", Z1_NODE_ID);
            }
            
            ping_response_pending = false;  // Clear the flag
        }
        
        loop_count++;
        sleep_ms(10);  // Small delay to prevent excessive polling
    }
    
    return 0;
}