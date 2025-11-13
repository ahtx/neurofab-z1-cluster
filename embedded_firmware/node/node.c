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
#include "z1_snn_engine.h"
#include "psram_rp2350.h"
#include "z1_multiframe.h"

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

// SNN engine state
static bool snn_initialized = false;
static bool snn_running = false;

// Multi-frame receive buffer
static uint8_t multiframe_buffer[4096];
static uint8_t multiframe_command = 0;

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
        // LED Control Commands
        case Z1_CMD_GREEN_LED:
            led_green_pwm = data;
            set_led_pwm(LED_GREEN, led_green_pwm);
            printf("[Node %d] ✅ GREEN LED PWM updated: %d/255\n", Z1_NODE_ID, data);
            break;
            
        case Z1_CMD_RED_LED:
            led_red_pwm = data;
            set_led_pwm(LED_RED, led_red_pwm);
            printf("[Node %d] ✅ RED LED PWM updated: %d/255\n", Z1_NODE_ID, data);
            break;
            
        case Z1_CMD_BLUE_LED:
            led_blue_pwm = data;
            set_led_pwm(LED_BLUE, led_blue_pwm);
            printf("[Node %d] ✅ BLUE LED PWM updated: %d/255\n", Z1_NODE_ID, data);
            break;
            
        case Z1_CMD_STATUS:
            printf("[Node %d] 📊 STATUS response - R:%d G:%d B:%d\n", 
                   Z1_NODE_ID, led_red_pwm, led_green_pwm, led_blue_pwm);
            break;
            
        case Z1_CMD_PING:
            printf("[Node %d] 🏓 PING received with data 0x%02X\n", Z1_NODE_ID, data);
            ping_response_pending = true;
            ping_response_target = z1_last_sender_id;
            ping_response_data = data;
            break;
        
        // Multi-frame Protocol Commands
        case Z1_CMD_FRAME_START:
            multiframe_command = data;  // Command is passed as data byte
            z1_multiframe_handle_start(z1_last_sender_id, data);
            printf("[Node %d] 📦 FRAME_START for command 0x%02X\n", Z1_NODE_ID, data);
            break;
            
        case Z1_CMD_FRAME_DATA:
            // data contains sequence number, next frame has actual data
            printf("[Node %d] 📦 FRAME_DATA seq %d\n", Z1_NODE_ID, data);
            break;
            
        case Z1_CMD_FRAME_END:
            z1_multiframe_handle_end(data);  // data is checksum
            printf("[Node %d] 📦 FRAME_END (checksum 0x%02X)\n", Z1_NODE_ID, data);
            
            // Process the received multi-frame command
            if (z1_multiframe_rx_complete()) {
                uint16_t length = z1_multiframe_rx_length();
                printf("[Node %d] Processing multiframe command 0x%02X (%d bytes)\n",
                       Z1_NODE_ID, multiframe_command, length);
                       
                // Handle the command based on type
                if (multiframe_command == Z1_CMD_MEM_WRITE) {
                    // Memory write: [addr:4][data:N]
                    if (length >= 4) {
                        uint32_t addr;
                        memcpy(&addr, multiframe_buffer, 4);
                        uint16_t data_len = length - 4;
                        printf("[Node %d] Writing %d bytes to PSRAM addr 0x%08X\n",
                               Z1_NODE_ID, data_len, (unsigned int)addr);
                        psram_write(addr, multiframe_buffer + 4, data_len);
                    }
                }
                
                z1_multiframe_rx_reset();
            }
            break;
        
        // SNN Engine Commands
        case Z1_CMD_SNN_LOAD_TABLE:
            if (snn_initialized) {
                printf("[Node %d] 🧠 Loading neuron table from PSRAM...\n", Z1_NODE_ID);
                // Assume table is at standard address
                uint32_t table_addr = 0x20100000;
                uint16_t neuron_count = data;  // Neuron count passed as data
                if (z1_snn_load_table(table_addr, neuron_count)) {
                    printf("[Node %d] ✅ Loaded %d neurons\n", Z1_NODE_ID, neuron_count);
                    set_led_pwm(LED_GREEN, 100);  // Green = loaded
                } else {
                    printf("[Node %d] ❌ Failed to load neurons\n", Z1_NODE_ID);
                    set_led_pwm(LED_RED, 100);  // Red = error
                }
            }
            break;
            
        case Z1_CMD_SNN_START:
            if (snn_initialized && !snn_running) {
                printf("[Node %d] 🧠 Starting SNN execution\n", Z1_NODE_ID);
                if (z1_snn_start()) {
                    snn_running = true;
                    set_led_pwm(LED_BLUE, 100);  // Blue = running
                    printf("[Node %d] ✅ SNN started\n", Z1_NODE_ID);
                }
            }
            break;
            
        case Z1_CMD_SNN_STOP:
            if (snn_running) {
                printf("[Node %d] 🧠 Stopping SNN execution\n", Z1_NODE_ID);
                z1_snn_stop();
                snn_running = false;
                set_led_pwm(LED_BLUE, 0);
                printf("[Node %d] ✅ SNN stopped\n", Z1_NODE_ID);
            }
            break;
            
        case Z1_CMD_SNN_INPUT_SPIKE:
            if (snn_running) {
                // data byte contains local neuron ID (low 8 bits)
                uint16_t local_neuron_id = data;
                printf("[Node %d] 🧠 Injecting spike to neuron %d\n", 
                       Z1_NODE_ID, local_neuron_id);
                z1_snn_inject_input(local_neuron_id, 1.0f);
            }
            break;
            
        case Z1_CMD_SNN_SPIKE:
            if (snn_running) {
                // Inter-node spike routing
                // data byte contains source node ID
                printf("[Node %d] 🧠 Received spike from node %d\n", Z1_NODE_ID, data);
                // Full spike data would come via multi-frame
            }
            break;
            
        default:
            printf("[Node %d] ❓ UNKNOWN command 0x%02X\n", Z1_NODE_ID, command);
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
    
    // Initialize PSRAM
    printf("Node %d: Initializing PSRAM...\n", Z1_NODE_ID);
    if (!psram_init()) {
        printf("Node %d: ❌ FATAL: Failed to initialize PSRAM\n", Z1_NODE_ID);
        return -1;
    }
    printf("Node %d: ✅ PSRAM initialized (8MB available)\n", Z1_NODE_ID);
    
    // Initialize multi-frame receive buffer
    z1_multiframe_rx_init(multiframe_buffer, sizeof(multiframe_buffer));
    printf("Node %d: ✅ Multi-frame RX buffer ready (%d bytes)\n", 
           Z1_NODE_ID, (int)sizeof(multiframe_buffer));
    
    // Initialize SNN engine
    printf("Node %d: Initializing SNN engine...\n", Z1_NODE_ID);
    if (!z1_snn_init(Z1_NODE_ID)) {
        printf("Node %d: ❌ WARNING: Failed to initialize SNN engine\n", Z1_NODE_ID);
        snn_initialized = false;
    } else {
        printf("Node %d: ✅ SNN engine initialized\n", Z1_NODE_ID);
        snn_initialized = true;
    }
    
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
        
        // Process SNN engine if running
        if (snn_running) {
            uint32_t current_time_us = time_us_32();
            z1_snn_step(current_time_us);
        }
        
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