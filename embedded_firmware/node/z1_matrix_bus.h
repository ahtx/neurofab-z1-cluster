#ifndef Z1_MATRIX_BUS_H
#define Z1_MATRIX_BUS_H

#include <stdint.h>
#include <stdbool.h>

// Z1 Matrix Bus GPIO Pin Definitions
// Multi-master 16-bit bus connecting 13 RP2350B nodes (12 nodes + 1 controller)

// Bus Control Pins
#define BUSATTN_PIN     2     // Bus attention signal - active low to claim bus
#define BUSACK_PIN      3     // Bus acknowledge - target confirms ready
#define BUSCLK_PIN      6     // Bus clock for synchronized transfers

// Address Bus (5-bit addressing: 0-31)
#define BUSSELECT0_PIN  7     // Address bit 0
#define BUSSELECT1_PIN  8     // Address bit 1  
#define BUSSELECT2_PIN  9     // Address bit 2
#define BUSSELECT3_PIN  10    // Address bit 3
#define BUSSELECT4_PIN  11    // Address bit 4

// Data Bus (16-bit bidirectional)
#define BUS0_PIN        12    // Data bit 0
#define BUS1_PIN        13    // Data bit 1
#define BUS2_PIN        14    // Data bit 2
#define BUS3_PIN        15    // Data bit 3
#define BUS4_PIN        16    // Data bit 4
#define BUS5_PIN        17    // Data bit 5
#define BUS6_PIN        18    // Data bit 6
#define BUS7_PIN        19    // Data bit 7
#define BUS8_PIN        20    // Data bit 8
#define BUS9_PIN        21    // Data bit 9
#define BUS10_PIN       22    // Data bit 10
#define BUS11_PIN       23    // Data bit 11
#define BUS12_PIN       24    // Data bit 12
#define BUS13_PIN       25    // Data bit 13
#define BUS14_PIN       26    // Data bit 14
#define BUS15_PIN       27    // Data bit 15

// Node ID Definitions
#define Z1_CONTROLLER_ID    16    // Primary controller node ID
#define Z1_BROADCAST_ID     31    // Broadcast to all nodes
#define Z1_MAX_NODES        16    // Maximum number of regular nodes (0-15)

// Protocol Constants
#define Z1_FRAME_HEADER     0xAA  // First byte of every connection
#define Z1_FRAMES_PER_MSG   2     // Header frame + command frame

// Command Definitions
#define Z1_CMD_GREEN_LED    0x10  // Green LED control (0-255 PWM)
#define Z1_CMD_RED_LED      0x20  // Red LED control (0-255 PWM)
#define Z1_CMD_BLUE_LED     0x30  // Blue LED control (0-255 PWM)
#define Z1_CMD_STATUS       0x40  // Status request
#define Z1_CMD_LED_CONTROL  0x70  // Generic LED control
#define Z1_CMD_PING         0x99  // Ping command (data: 0xA5)

// SNN Engine Commands
#define Z1_CMD_SNN_SPIKE         0x50  // SNN spike routing command
#define Z1_CMD_SNN_LOAD_TABLE    0x51  // Load neuron table from PSRAM
#define Z1_CMD_SNN_START         0x52  // Start SNN execution
#define Z1_CMD_SNN_STOP          0x53  // Stop SNN execution
#define Z1_CMD_SNN_INJECT_SPIKE  0x54  // Inject external spike
#define Z1_CMD_SNN_GET_STATUS    0x55  // Get SNN engine status

// LED Values for generic LED control
#define Z1_LED_GREEN        0x01
#define Z1_LED_RED          0x02

// Ping Data
#define Z1_PING_DATA        0xA5  // Standard ping payload

// Legacy compatibility
#define Z1_BROADCAST_ADDR   Z1_BROADCAST_ID
#define BUSACT_PIN          BUSATTN_PIN

// Timing Configuration (microseconds)
extern volatile uint32_t z1_bus_clock_high_us;   // Clock high time between frames
extern volatile uint32_t z1_bus_clock_low_us;    // Clock low time for data latch
extern volatile uint32_t z1_bus_ack_timeout_ms;  // ACK wait timeout
extern volatile uint32_t z1_bus_backoff_base_us; // Base backoff time for collisions
extern volatile uint32_t z1_bus_broadcast_hold_ms; // Time to hold BUSATTN low for broadcast

// Ping timing configuration (milliseconds)
extern volatile uint32_t z1_ping_response_wait_ms; // Controller waits for responses
extern volatile uint32_t z1_ping_node_delay_ms;    // Nodes wait before responding

// Bus State Variables
extern volatile uint8_t z1_last_sender_id;       // ID of last sender (from 0xAA frame)
extern volatile bool z1_bus_transaction_active;  // True when transaction in progress

// Function Prototypes
bool z1_bus_init(uint8_t node_id);
bool z1_bus_write(uint8_t target_node, uint8_t command, uint8_t data);
bool z1_bus_broadcast(uint8_t command, uint8_t data);
void z1_bus_handle_interrupt(void);

// Ping functions
bool z1_bus_ping_node(uint8_t target_node);
bool z1_bus_ping_all_nodes(void);
void z1_bus_clear_ping_history(void);
bool z1_bus_handle_ping_response(uint8_t sender_node, uint8_t data_received);

// Node discovery - sequential ping with proper timing
bool z1_discover_nodes_sequential(bool active_nodes_out[16]);

// Callback function - must be implemented by application (node.c)
extern void z1_bus_process_command(uint8_t command, uint8_t data);

// ============================================================================
// SNN Engine Compatibility Layer
// ============================================================================

// Message structure for SNN engine compatibility
typedef struct {
    uint8_t source_node;
    uint8_t command;
    uint8_t data[16];  // Extended data payload
} z1_bus_message_t;

// Receive queue for incoming bus messages (for SNN engine)
bool z1_bus_receive(z1_bus_message_t* msg);

#endif // Z1_MATRIX_BUS_H
