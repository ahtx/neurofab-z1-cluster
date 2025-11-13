#include "z1_matrix_bus.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdio.h>

// Simple Linear Congruential Generator for collision avoidance
// Uses parameters from Numerical Recipes: a=1664525, c=1013904223, m=2^32
static uint32_t z1_random_state = 1;

static void z1_random_seed(uint32_t seed) {
    z1_random_state = seed ? seed : 1;  // Avoid zero seed
}

static uint32_t z1_random(void) {
    z1_random_state = z1_random_state * 1664525U + 1013904223U;
    return z1_random_state;
}

// Static variables
static uint8_t my_node_id = 0;
static bool bus_initialized = false;

// Timing Configuration (microseconds) - Global tunable variables
volatile uint32_t z1_bus_clock_high_us = 100;    // Clock high time between frames
volatile uint32_t z1_bus_clock_low_us = 50;      // Clock low time for data latch
volatile uint32_t z1_bus_ack_timeout_ms = 500;   // ACK wait timeout
volatile uint32_t z1_bus_backoff_base_us = 100;  // Base backoff time for collisions
volatile uint32_t z1_bus_broadcast_hold_ms = 50; // Time to hold BUSATTN low for broadcast

// Ping timing configuration (milliseconds) - Global adjustable variables
volatile uint32_t z1_ping_response_wait_ms = 1500;  // Controller waits 1500ms for responses (nodes now wait 200ms+ before responding)
volatile uint32_t z1_ping_node_delay_ms = 10;      // Nodes wait 10ms before responding

// Bus State Variables
volatile uint8_t z1_last_sender_id = 0;          // ID of last sender (from 0xAA frame)
volatile bool z1_bus_transaction_active = false; // True when transaction in progress
volatile bool z1_irq_handler_busy = false;       // Prevent interrupt re-entrance

// Ping tracking for controller node
#define Z1_MAX_PING_HISTORY 20
typedef struct {
    uint8_t target_node;
    uint8_t data_sent;
    uint32_t timestamp_ms;
    bool active;
} z1_ping_entry_t;

static z1_ping_entry_t ping_history[Z1_MAX_PING_HISTORY];
static uint8_t ping_history_index = 0;

// Forward declaration for interrupt handler
void z1_busattn_irq_handler(uint gpio, uint32_t events);

// Set all bus pins to input (high-impedance) - default listening state
void z1_bus_set_all_pins_input(void) {
    // Control pins to input
    gpio_set_dir(BUSATTN_PIN, GPIO_IN);
    gpio_set_dir(BUSACK_PIN, GPIO_IN);
    gpio_set_dir(BUSCLK_PIN, GPIO_IN);
    
    // Address pins to input
    for (int i = 0; i < 5; i++) {
        gpio_set_dir(BUSSELECT0_PIN + i, GPIO_IN);
    }
    
    // Data pins to input
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_IN);
        gpio_disable_pulls(BUS0_PIN + i);
    }
    
    printf("[Z1 Bus] All pins set to input (listening mode)\n");
}

// Set address on the bus select lines
void z1_bus_set_address(uint8_t address) {
    // Set address pins to output first
    for (int i = 0; i < 5; i++) {
        gpio_set_dir(BUSSELECT0_PIN + i, GPIO_OUT);
    }
    
    // Set address bits
    gpio_put(BUSSELECT0_PIN, (address >> 0) & 1);
    gpio_put(BUSSELECT1_PIN, (address >> 1) & 1);
    gpio_put(BUSSELECT2_PIN, (address >> 2) & 1);
    gpio_put(BUSSELECT3_PIN, (address >> 3) & 1);
    gpio_put(BUSSELECT4_PIN, (address >> 4) & 1);
    
    printf("[Z1 Bus] Address set to %d (0x%02X)\n", address, address);
}

// Set data on the 16-bit data bus
void z1_bus_set_data(uint16_t data) {
    // Set data bus to output
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_OUT);
        gpio_put(BUS0_PIN + i, (data >> i) & 1);
    }
    printf("[Z1 Bus] Data set to 0x%04X\n", data);
}

// Read data from the 16-bit data bus
uint16_t z1_bus_get_data(void) {
    uint16_t data = 0;
    for (int i = 0; i < 16; i++) {
        if (gpio_get(BUS0_PIN + i)) {
            data |= (1 << i);
        }
    }
    return data;
}

// Read address from the bus select lines
uint8_t z1_bus_get_address(void) {
    uint8_t address = 0;
    
    if (gpio_get(BUSSELECT0_PIN)) address |= 1;
    if (gpio_get(BUSSELECT1_PIN)) address |= 2;
    if (gpio_get(BUSSELECT2_PIN)) address |= 4;
    if (gpio_get(BUSSELECT3_PIN)) address |= 8;
    if (gpio_get(BUSSELECT4_PIN)) address |= 16;
    
    return address;
}

// Claim the bus with exponential backoff + jitter to prevent collisions
bool z1_bus_claim_bus(void) {
    uint32_t backoff_us = z1_bus_backoff_base_us;
    uint32_t max_attempts = 10;
    
    for (uint32_t attempt = 0; attempt < max_attempts; attempt++) {
        // Check if BUSATTN is high (bus available)
        if (gpio_get(BUSATTN_PIN)) {
            printf("[Z1 Bus] Bus available, claiming...\n");
            
            // Disable interrupt while we're sending to avoid self-triggering
            gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, false);
            
            // Immediately claim bus
            gpio_set_dir(BUSATTN_PIN, GPIO_OUT);
            gpio_put(BUSATTN_PIN, 0);  // Drive low to claim
            
            // Set clock to output and high
            gpio_set_dir(BUSCLK_PIN, GPIO_OUT);
            gpio_put(BUSCLK_PIN, 1);   // Clock high
            
            printf("[Z1 Bus] ✅ Bus claimed successfully\n");
            z1_bus_transaction_active = true;
            return true;
        }
        
        // Bus busy, exponential backoff with random jitter to prevent synchronized collisions
        uint32_t jitter_us = (z1_random() % (backoff_us / 2 + 1));  // 0 to 50% jitter
        uint32_t total_backoff = backoff_us + jitter_us;
        printf("[Z1 Bus] Bus busy, attempt %d, backing off %dus (base: %dus, jitter: +%dus)\n", 
               attempt + 1, total_backoff, backoff_us, jitter_us);
        sleep_us(total_backoff);
        backoff_us *= 2;
        if (backoff_us > 10000) backoff_us = 10000; // Max 10ms
    }
    
    printf("[Z1 Bus] ❌ Failed to claim bus after %d attempts\n", max_attempts);
    return false;
}

// Release the bus - return all pins to listening state
void z1_bus_release_bus(void) {
    printf("[Z1 Bus] Releasing bus\n");
    z1_bus_set_all_pins_input();
    z1_bus_transaction_active = false;
    
    // Re-enable interrupt for receiving
    gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, true);
}

// Wait for receiver to pull BUSACK low
bool z1_bus_wait_for_ack(void) {
    printf("[Z1 Bus] Waiting for ACK (BUSACK to go low)\n");
    
    absolute_time_t timeout_time = make_timeout_time_ms(z1_bus_ack_timeout_ms);
    uint32_t poll_count = 0;
    
    while (!time_reached(timeout_time)) {
        if (!gpio_get(BUSACK_PIN)) {  // ACK is active low
            printf("[Z1 Bus] ✅ ACK received after %d polls\n", poll_count);
            return true;
        }
        poll_count++;
        if (poll_count % 1000 == 0) {
            printf("[Z1 Bus] Still waiting for ACK... polls=%d\n", poll_count);
        }
        sleep_us(10);
    }
    
    printf("[Z1 Bus] ❌ ACK timeout after %d polls\n", poll_count);
    return false;
}

// Send a single 16-bit frame with proper clock timing
void z1_bus_send_frame(uint16_t frame_data, bool is_last_frame) {
    printf("[Z1 Bus] Sending frame: 0x%04X (last=%s)\n", frame_data, is_last_frame ? "YES" : "NO");
    
    // Set data on bus
    z1_bus_set_data(frame_data);
    
    // Wait for ACK from receiver
    if (!z1_bus_wait_for_ack()) {
        printf("[Z1 Bus] ❌ No ACK for frame 0x%04X\n", frame_data);
        return;
    }
    
    // Drop clock low - receiver latches data on falling edge
    printf("[Z1 Bus] Dropping clock LOW for data latch\n");
    gpio_put(BUSCLK_PIN, 0);
    sleep_us(z1_bus_clock_low_us);
    
    if (!is_last_frame) {
        // Bring clock back high for next frame
        printf("[Z1 Bus] Clock back HIGH (more frames)\n");
        gpio_put(BUSCLK_PIN, 1);
        sleep_us(z1_bus_clock_high_us);
    } else {
        // Last frame - keep clock low and data valid until receiver releases BUSACK
        printf("[Z1 Bus] Clock stays LOW (last frame - waiting for BUSACK release)\n");
    }
}

// Initialize the Z1 Matrix Bus
bool z1_bus_init(uint8_t node_id) {
    my_node_id = node_id;
    
    // Initialize random seed for backoff jitter (each node gets different seed)
    z1_random_seed(get_absolute_time() + node_id);
    
    printf("[Z1 Bus] Initializing bus for node ID %d\n", node_id);
    
    // Initialize all GPIO pins
    for (int pin = BUSATTN_PIN; pin <= BUS15_PIN; pin++) {
        gpio_init(pin);
    }
    
    // Set all pins to input (listening mode) with pull-ups on control pins
    z1_bus_set_all_pins_input();
    gpio_pull_up(BUSATTN_PIN);  // 330Ω resistor tie-high
    gpio_pull_up(BUSACK_PIN);   // 330Ω resistor tie-high
    
    // Set up interrupt on BUSATTN falling edge
    gpio_set_irq_enabled_with_callback(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, true, &z1_busattn_irq_handler);
    
    bus_initialized = true;
    printf("[Z1 Bus] ✅ Bus initialization complete for node %d\n", node_id);
    return true;
}

// Write command and data to target node
bool z1_bus_write(uint8_t target_node, uint8_t command, uint8_t data) {
    if (!bus_initialized) {
        printf("[Z1 Bus] ❌ Bus not initialized\n");
        return false;
    }
    
    printf("[Z1 Bus] === STARTING WRITE TRANSACTION ===\n");
    printf("[Z1 Bus] Target: %d, Command: 0x%02X, Data: 0x%02X\n", target_node, command, data);
    
    // Try to claim the bus
    if (!z1_bus_claim_bus()) {
        return false;
    }
    
    // Set target address
    z1_bus_set_address(target_node);
    
    // Frame 1: Header (0xAA + my_node_id)
    uint16_t frame1 = (Z1_FRAME_HEADER << 8) | my_node_id;
    printf("[Z1 Bus] Frame 1: Header 0x%02X + Sender ID %d\n", Z1_FRAME_HEADER, my_node_id);
    z1_bus_send_frame(frame1, false);  // Not last frame
    
    // Frame 2: Command + Data
    uint16_t frame2 = (command << 8) | data;
    printf("[Z1 Bus] Frame 2: Command 0x%02X + Data 0x%02X\n", command, data);
    z1_bus_send_frame(frame2, true);   // Last frame - clock stays low
    
    // Keep data valid on bus, wait for receiver to release BUSACK
    printf("[Z1 Bus] Waiting for receiver to release BUSACK...\n");
    absolute_time_t timeout_time = make_timeout_time_ms(z1_bus_ack_timeout_ms);
    while (!time_reached(timeout_time) && !gpio_get(BUSACK_PIN)) {
        sleep_us(10);
    }
    
    if (!gpio_get(BUSACK_PIN)) {
        printf("[Z1 Bus] ⚠️ Receiver did not release BUSACK\n");
    } else {
        printf("[Z1 Bus] ✅ Receiver released BUSACK - transaction complete\n");
    }
    
    // Release the bus
    z1_bus_release_bus();
    
    printf("[Z1 Bus] === WRITE TRANSACTION COMPLETE ===\n");
    return true;
}

// Broadcast command to all nodes (no ACK, no clock)
bool z1_bus_broadcast(uint8_t command, uint8_t data) {
    if (!bus_initialized) {
        printf("[Z1 Bus] ❌ Bus not initialized\n");
        return false;
    }
    
    printf("[Z1 Bus] === STARTING BROADCAST TRANSACTION ===\n");
    printf("[Z1 Bus] Command: 0x%02X, Data: 0x%02X (broadcast to all nodes)\n", command, data);
    
    // Manual bus claiming for broadcast (no normal claim sequence)
    if (!gpio_get(BUSATTN_PIN)) {
        printf("[Z1 Bus] ❌ Bus busy for broadcast\n");
        return false;
    }
    
    printf("[Z1 Bus] Claiming bus for broadcast\n");
    z1_bus_transaction_active = true;
    
    // Disable interrupt while we're transmitting
    gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, false);
    
    // Claim BUSATTN (drive low)
    gpio_set_dir(BUSATTN_PIN, GPIO_OUT);
    gpio_put(BUSATTN_PIN, 0);
    
    // Set address to broadcast
    for (int i = 0; i < 5; i++) {
        gpio_set_dir(BUSSELECT0_PIN + i, GPIO_OUT);
        gpio_put(BUSSELECT0_PIN + i, (Z1_BROADCAST_ID >> i) & 1);
    }
    
    // Set data bus to output and put command+data
    uint16_t broadcast_data = (command << 8) | data;
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_OUT);
        gpio_put(BUS0_PIN + i, (broadcast_data >> i) & 1);
    }
    
    printf("[Z1 Bus] BUSATTN=LOW, Address=31, Data=0x%04X, holding for %dms\n", 
           broadcast_data, z1_bus_broadcast_hold_ms);
    
    // Hold for configured time
    sleep_ms(z1_bus_broadcast_hold_ms);
    
    // Release BUSATTN (triggers data latch in all nodes)
    printf("[Z1 Bus] Releasing BUSATTN (data latch trigger)\n");
    gpio_set_dir(BUSATTN_PIN, GPIO_IN);
    gpio_pull_up(BUSATTN_PIN);
    
    // Release all other pins
    z1_bus_set_all_pins_input();
    z1_bus_transaction_active = false;
    
    // Re-enable interrupt
    gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, true);
    
    printf("[Z1 Bus] === BROADCAST TRANSACTION COMPLETE ===\n");
    return true;
}

// BUSATTN interrupt handler - called when someone claims the bus
void z1_busattn_irq_handler(uint gpio, uint32_t events) {
    if (gpio != BUSATTN_PIN || !(events & GPIO_IRQ_EDGE_FALL)) {
        return;
    }
    
    // Prevent re-entrance
    if (z1_irq_handler_busy) {
        return;  // Already processing an interrupt
    }
    z1_irq_handler_busy = true;
    
    // If we're already in a transaction (sending), ignore this interrupt
    if (z1_bus_transaction_active) {
        z1_irq_handler_busy = false;
        return;
    }
    
    // Small delay for signal settling
    sleep_us(20);
    
    // Read address to see if it's for us
    uint8_t target_address = z1_bus_get_address();
    
    // Check if message is for us (direct or broadcast)
    if (target_address != my_node_id && target_address != Z1_BROADCAST_ID) {
        z1_irq_handler_busy = false;  // Clear busy flag before returning
        return;
    }
    
    if (target_address == Z1_BROADCAST_ID) {
        z1_bus_transaction_active = true;
        
        // Set data bus to INPUT for receiving (but DO NOT touch BUSACK)
        for (int i = 0; i < 16; i++) {
            gpio_set_dir(BUS0_PIN + i, GPIO_IN);
            gpio_disable_pulls(BUS0_PIN + i);
        }
        
        // Wait for BUSATTN to go high (sender releases after hold time)
        uint32_t timeout_count = 0;
        while (!gpio_get(BUSATTN_PIN)) {
            sleep_us(100);
            timeout_count++;
            if (timeout_count > 1000) {  // 100ms timeout (2x hold time)
                z1_bus_transaction_active = false;
                z1_irq_handler_busy = false;
                return;
            }
        }
        
        // Latch the 16-bit command+data immediately
        uint16_t received_data = z1_bus_get_data();
        uint8_t command = (received_data >> 8) & 0xFF;
        uint8_t data_value = received_data & 0xFF;
        
        // Process broadcast command immediately
        z1_bus_process_command(command, data_value);
        
        z1_bus_transaction_active = false;
        z1_irq_handler_busy = false;
        return;
    }
    
    // Normal targeted transaction
    z1_bus_transaction_active = true;
    
    // Set data bus pins to INPUT for receiving
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_IN);
        gpio_disable_pulls(BUS0_PIN + i);
    }
    
    // Pull BUSACK low to signal ready
    gpio_set_dir(BUSACK_PIN, GPIO_OUT);
    gpio_put(BUSACK_PIN, 0);
    
    // Receive exactly 2 frames according to protocol
    uint8_t command = 0, data_value = 0;
    
    for (int frame = 0; frame < 2; frame++) {
        // Wait for clock to drop (sender drops after we ACK)
        uint32_t timeout_count = 0;
        while (gpio_get(BUSCLK_PIN)) {
            sleep_us(1);
            timeout_count++;
            if (timeout_count > 50000) {  // 50ms timeout
                goto cleanup;
            }
        }
        
        // Latch data on clock falling edge
        uint16_t received_data = z1_bus_get_data();
        
        if (frame == 0) {
            // Frame 1: Header + Sender ID
            uint8_t header = (received_data >> 8) & 0xFF;
            uint8_t sender_id = received_data & 0xFF;
            
            if (header == Z1_FRAME_HEADER) {
                z1_last_sender_id = sender_id;
            } else {
                goto cleanup;
            }
        } else if (frame == 1) {
            // Frame 2: Command + Data
            command = (received_data >> 8) & 0xFF;
            data_value = received_data & 0xFF;
        }
        
        // Wait for clock to go high again (except on last frame)
        if (frame == 0) {
            timeout_count = 0;
            while (!gpio_get(BUSCLK_PIN)) {
                sleep_us(1);
                timeout_count++;
                if (timeout_count > 50000) {  // 50ms timeout
                    goto cleanup;
                }
            }
        }
    }
    
    // Release BUSACK first to complete the receive transaction
    gpio_set_dir(BUSACK_PIN, GPIO_IN);
    gpio_pull_up(BUSACK_PIN);
    z1_bus_transaction_active = false;
    
    // Now process the command (transaction is complete, node can send responses)
    z1_bus_process_command(command, data_value);
    
    // Clear busy flag
    z1_irq_handler_busy = false;
    return;

cleanup:
    // Release BUSACK to signal completion
    gpio_set_dir(BUSACK_PIN, GPIO_IN);
    gpio_pull_up(BUSACK_PIN);
    z1_bus_transaction_active = false;
    
    // Clear busy flag
    z1_irq_handler_busy = false;
}

// Handle incoming bus transactions (polling version for main loop)
void z1_bus_handle_interrupt(void) {
    // This function is now handled by the interrupt system
    // Main loop can call this periodically if needed for additional processing
    if (z1_bus_transaction_active) {
        // Transaction in progress via interrupt
        return;
    }
}

// Add a ping to the tracking history
static void z1_ping_add_to_history(uint8_t target_node, uint8_t data_sent) {
    ping_history[ping_history_index].target_node = target_node;
    ping_history[ping_history_index].data_sent = data_sent;
    ping_history[ping_history_index].timestamp_ms = to_ms_since_boot(get_absolute_time());
    ping_history[ping_history_index].active = true;
    
    ping_history_index = (ping_history_index + 1) % Z1_MAX_PING_HISTORY;
    printf("[Z1 Ping] Added ping to history: node %d, data 0x%02X\n", target_node, data_sent);
}

// Check if a received ping response matches our sent ping
static bool z1_ping_check_response(uint8_t sender_node, uint8_t data_received) {
    for (int i = 0; i < Z1_MAX_PING_HISTORY; i++) {
        if (ping_history[i].active && 
            ping_history[i].target_node == sender_node && 
            ping_history[i].data_sent == data_received) {
            
            uint32_t response_time = to_ms_since_boot(get_absolute_time()) - ping_history[i].timestamp_ms;
            printf("[Z1 Ping] ✅ PING RESPONSE from node %d: %dms (data match: 0x%02X)\n", 
                   sender_node, response_time, data_received);
            
            // Mark as inactive - ping completed
            ping_history[i].active = false;
            return true;
        }
    }
    
    printf("[Z1 Ping] ⚠️ Unexpected ping response from node %d (data: 0x%02X)\n", 
           sender_node, data_received);
    return false;
}

// Ping a specific node
bool z1_bus_ping_node(uint8_t target_node) {
    if (!bus_initialized) {
        printf("[Z1 Ping] ❌ Bus not initialized\n");
        return false;
    }
    
    if (target_node >= Z1_MAX_NODES) {
        printf("[Z1 Ping] ❌ Invalid node ID: %d\n", target_node);
        return false;
    }
    
    printf("[Z1 Ping] 🏓 PING node %d with data 0x%02X\n", target_node, Z1_PING_DATA);
    
    // Add to ping history first
    z1_ping_add_to_history(target_node, Z1_PING_DATA);
    
    // Send ping using existing bus write function
    return z1_bus_write(target_node, Z1_CMD_PING, Z1_PING_DATA);
}

// Clear ping history (useful before starting a new ping test)
void z1_bus_clear_ping_history(void) {
    printf("[Z1 Ping] 🧹 Clearing ping history\n");
    for (int i = 0; i < Z1_MAX_PING_HISTORY; i++) {
        ping_history[i].target_node = 0xFF;  // Invalidate entry
        ping_history[i].active = false;
    }
}

// Ping all nodes (0-11)
bool z1_bus_ping_all_nodes(void) {
    if (!bus_initialized) {
        printf("[Z1 Ping] ❌ Bus not initialized\n");
        return false;
    }
    
    // Clear all old ping history to prevent showing stale responses
    printf("[Z1 Ping] 🧹 Clearing old ping history\n");
    for (int i = 0; i < Z1_MAX_PING_HISTORY; i++) {
        ping_history[i].target_node = 0xFF;  // Invalidate entry
        ping_history[i].active = false;
    }
    
    printf("[Z1 Ping] 🏓 PING ALL NODES (0-%d) - No delay between pings\n", Z1_MAX_NODES - 1);
    
    bool all_success = true;
    int ping_count = 0;
    for (uint8_t node = 0; node < Z1_MAX_NODES; node++) {
        if (!z1_bus_ping_node(node)) {
            all_success = false;
        } else {
            ping_count++;
        }
        
        // CRITICAL: Re-enable interrupts after each ping so we can receive responses
        gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, true);
        
        // NO DELAY - send pings as fast as the bus allows
    }
    
    printf("[Z1 Ping] 📤 Sent %d pings\n", ping_count);
    
    // Wait for responses using configurable timeout
    printf("[Z1 Ping] ⏱️ Waiting %dms for responses...\n", z1_ping_response_wait_ms);
    
    // CRITICAL: Ensure interrupts are enabled to receive responses
    gpio_set_irq_enabled(BUSATTN_PIN, GPIO_IRQ_EDGE_FALL, true);
    printf("[Z1 Ping] 🔄 Interrupts enabled for response reception\n");
    
    sleep_ms(z1_ping_response_wait_ms);
    
    printf("[Z1 Ping] ⏱️ Response wait period complete\n");
    
    return all_success;
}

// Check if a specific node has responded to its ping (helper for discovery)
static bool check_if_node_responded(uint8_t node) {
    for (int i = 0; i < Z1_MAX_PING_HISTORY; i++) {
        if (ping_history[i].target_node == node && !ping_history[i].active) {
            // Found a completed ping response for this node
            return true;
        }
    }
    return false;
}

// Sequential node discovery - pings nodes one at a time with proper delays
bool z1_discover_nodes_sequential(bool active_nodes_out[16]) {
    if (!bus_initialized) {
        printf("[Z1 Discovery] ❌ Bus not initialized\n");
        return false;
    }
    
    // Clear all nodes before discovery
    for (int i = 0; i < 16; i++) {
        active_nodes_out[i] = false;
    }
    
    // Use fast timeout for parallel bus (30ms = 3x expected 10ms response)
    uint32_t per_node_timeout = 30;
    printf("[Z1 Discovery] 🔍 Starting sequential node discovery (0-15)\n");
    printf("[Z1 Discovery] Per-node timeout: %dms (fast parallel bus)\n", per_node_timeout);
    
    // Give bus time to settle before starting discovery
    sleep_ms(20);
    
    uint8_t discovered_count = 0;
    
    for (uint8_t node = 0; node < 16; node++) {
        printf("[Z1 Discovery] 🏓 Pinging node %d...\n", node);
        
        // Clear old ping history for this node (invalidate by setting target to 0xFF)
        for (int i = 0; i < Z1_MAX_PING_HISTORY; i++) {
            if (ping_history[i].target_node == node) {
                ping_history[i].target_node = 0xFF;  // Invalidate entry
                ping_history[i].active = false;
            }
        }
        
        // Send ping to this node
        if (!z1_bus_ping_node(node)) {
            printf("[Z1 Discovery] ⚠️  Node %d: ping send failed\n", node);
            active_nodes_out[node] = false;
            continue;
        }
        
        // Wait for response with timeout
        uint32_t start_time = to_ms_since_boot(get_absolute_time());
        bool responded = false;
        
        while ((to_ms_since_boot(get_absolute_time()) - start_time) < per_node_timeout) {
            // Process any incoming bus messages (including ping responses)
            z1_bus_handle_interrupt();
            
            // Check if we got a response
            if (check_if_node_responded(node)) {
                responded = true;
                break;
            }
            sleep_ms(10);  // Small polling interval
        }
        
        active_nodes_out[node] = responded;
        
        if (responded) {
            uint32_t response_time = to_ms_since_boot(get_absolute_time()) - start_time;
            printf("[Z1 Discovery] ✅ Node %d: ACTIVE (responded in %dms)\n", node, response_time);
            discovered_count++;
        } else {
            printf("[Z1 Discovery] ❌ Node %d: offline (no response)\n", node);
        }
        
        // Small delay between nodes to ensure bus is idle
        sleep_ms(5);
    }
    
    printf("[Z1 Discovery] 🔍 Discovery complete: %d/%d nodes active\n", discovered_count, 16);
    return true;
}

// Handle ping response (public interface for controller)
bool z1_bus_handle_ping_response(uint8_t sender_node, uint8_t data_received) {
    return z1_ping_check_response(sender_node, data_received);
}

// Cleanup bus resources
// Weak default implementation - can be overridden by application
__attribute__((weak)) void z1_bus_process_command(uint8_t command, uint8_t data) {
    printf("[Z1 Bus] WEAK DEFAULT: Command 0x%02X, Data 0x%02X (no application handler)\n", command, data);
}

// ============================================================================
// SNN Engine Compatibility Layer
// ============================================================================

// Simple message queue for SNN engine (stub implementation)
// In a full implementation, this would use a circular buffer filled by interrupts
static z1_bus_message_t message_queue[8];
static volatile uint8_t queue_head = 0;
static volatile uint8_t queue_tail = 0;

/**
 * Receive a message from the bus queue (for SNN engine)
 * Returns false if queue is empty
 */
bool z1_bus_receive(z1_bus_message_t* msg) {
    if (queue_head == queue_tail) {
        return false;  // Queue empty
    }
    
    *msg = message_queue[queue_tail];
    queue_tail = (queue_tail + 1) % 8;
    return true;
}

/**
 * Internal function to queue a received message
 * Called by interrupt handler or bus processing code
 */
void z1_bus_queue_message(uint8_t source_node, uint8_t command, uint8_t data) {
    uint8_t next_head = (queue_head + 1) % 8;
    if (next_head == queue_tail) {
        return;  // Queue full, drop message
    }
    
    message_queue[queue_head].source_node = source_node;
    message_queue[queue_head].command = command;
    message_queue[queue_head].data[0] = data;
    queue_head = next_head;
}
