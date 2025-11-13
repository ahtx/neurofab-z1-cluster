/**
 * Z1 Cluster Controller - Complete Implementation with W5500 HTTP Server
 * 
 * Integrates:
 * - W5500 Ethernet HTTP server
 * - SNN API endpoints
 * - SSD1306 OLED display status
 * - Matrix bus cluster management
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

// Project headers
#include "z1_matrix_bus.h"
#include "z1_protocol_extended.h"
#include "z1_http_api.h"
#include "ssd1306.h"
#include "psram_rp2350.h"

// Controller is always node ID 16
#define Z1_CONTROLLER_NODE_ID  16

// W5500 Pin Definitions
#define W5500_SPI_PORT spi0
#define W5500_SCK      38
#define W5500_MOSI     39
#define W5500_MISO     36
#define W5500_CS       37
#define W5500_RST      34
#define W5500_INT      35

// I2C OLED Display Pins
#define OLED_I2C       i2c0
#define OLED_SDA_PIN   28
#define OLED_SCL_PIN   29

// LED pins
#define LED_RED_PIN    44
#define LED_GREEN_PIN  45
#define LED_BLUE_PIN   46

// W5500 Register Definitions
#define W5500_MR         0x0000
#define W5500_SHAR0      0x0009
#define W5500_GAR0       0x0001
#define W5500_SUBR0      0x0005
#define W5500_SIPR0      0x000F
#define W5500_VERSIONR   0x0039
#define W5500_PHYCFGR    0x002E

// PHY Configuration
#define PHYCFGR_RST      0x80
#define PHYCFGR_DPX      0x04
#define PHYCFGR_SPD      0x02
#define PHYCFGR_LNK      0x01

// Socket Registers
#define S0_MR            0x0000
#define S0_CR            0x0001
#define S0_IR            0x0002
#define S0_SR            0x0003
#define S0_PORT0         0x0004

// Block Select Bits
#define COMMON_REG_BSB   0x00
#define SOCKET0_REG_BSB  0x08

// Socket Commands
#define SOCK_OPEN        0x01
#define SOCK_LISTEN      0x02
#define SOCK_DISCON      0x08
#define SOCK_CLOSE       0x10
#define SOCK_SEND        0x20
#define SOCK_RECV        0x40

// Socket Status
#define SOCK_STAT_CLOSED      0x00
#define SOCK_STAT_INIT        0x13
#define SOCK_STAT_LISTEN      0x14
#define SOCK_STAT_ESTABLISHED 0x17
#define SOCK_STAT_CLOSE_WAIT  0x1C
#define SOCK_TCP              0x01

// Network Configuration
const uint8_t MAC_ADDRESS[] = {0x02, 0x08, 0xDC, 0x00, 0x00, 0x01};
const uint8_t IP_ADDRESS[]  = {192, 168, 1, 222};
const uint8_t GATEWAY[]     = {192, 168, 1, 254};
const uint8_t SUBNET_MASK[] = {255, 255, 255, 0};

// Global state
static bool oled_initialized = false;
static bool http_connection_active = false;
static uint8_t active_node_count = 0;
static char current_status[64] = "Initializing...";

// ============================================================================
// OLED Display Functions
// ============================================================================

void update_oled_status(const char* status) {
    printf("[Status] %s\n", status);
    
    if (!oled_initialized) {
        return;
    }
    
    strncpy(current_status, status, sizeof(current_status) - 1);
    current_status[sizeof(current_status) - 1] = '\0';
    
    // Clear display
    ssd1306_display_clear();
    
    // Header
    ssd1306_write_line("Z1 SNN Controller", 0);
    ssd1306_draw_line(0, 10, 127, 10);
    
    // IP Address
    char ip_str[22];
    snprintf(ip_str, sizeof(ip_str), "IP:%d.%d.%d.%d", 
             IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    ssd1306_write_line(ip_str, 2);
    
    // Connection status
    ssd1306_write_line(http_connection_active ? "HTTP: Connected" : "HTTP: Listening", 3);
    
    // Node count
    char node_str[22];
    snprintf(node_str, sizeof(node_str), "Nodes: %d/16", active_node_count);
    ssd1306_write_line(node_str, 4);
    
    // Current status (split if needed)
    if (strlen(status) > 21) {
        char line1[22], line2[22];
        strncpy(line1, status, 21);
        line1[21] = '\0';
        strncpy(line2, status + 21, 21);
        line2[21] = '\0';
        ssd1306_write_line(line1, 6);
        ssd1306_write_line(line2, 7);
    } else {
        ssd1306_write_line(status, 6);
    }
    
    ssd1306_display_update();
}

// ============================================================================
// LED Control Functions
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
    pwm_set_gpio_level(LED_GREEN_PIN, 50);
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
// W5500 Driver Functions
// ============================================================================

static void w5500_select(void) {
    gpio_put(W5500_CS, 0);
    sleep_us(1);
}

static void w5500_deselect(void) {
    sleep_us(1);
    gpio_put(W5500_CS, 1);
    sleep_us(1);
}

uint8_t w5500_read_reg(uint16_t addr, uint8_t bsb) {
    uint8_t data;
    w5500_select();
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr >> 8}, 1);
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr & 0xFF}, 1);
    spi_write_blocking(W5500_SPI_PORT, &bsb, 1);
    spi_read_blocking(W5500_SPI_PORT, 0x00, &data, 1);
    w5500_deselect();
    return data;
}

void w5500_write_reg(uint16_t addr, uint8_t bsb, uint8_t data) {
    w5500_select();
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr >> 8}, 1);
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr & 0xFF}, 1);
    uint8_t control = bsb | 0x04;
    spi_write_blocking(W5500_SPI_PORT, &control, 1);
    spi_write_blocking(W5500_SPI_PORT, &data, 1);
    w5500_deselect();
}

static void w5500_hardware_reset(void) {
    printf("[W5500] Hardware reset\n");
    gpio_put(W5500_RST, 0);
    sleep_ms(10);
    gpio_put(W5500_RST, 1);
    sleep_ms(200);
}

static bool w5500_init(void) {
    printf("[W5500] Initializing...\n");
    update_oled_status("Init W5500...");
    
    // Initialize SPI
    spi_init(W5500_SPI_PORT, 40000000);
    gpio_set_function(W5500_SCK, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MISO, GPIO_FUNC_SPI);
    
    gpio_init(W5500_CS);
    gpio_set_dir(W5500_CS, GPIO_OUT);
    gpio_put(W5500_CS, 1);
    
    gpio_init(W5500_RST);
    gpio_set_dir(W5500_RST, GPIO_OUT);
    gpio_put(W5500_RST, 1);
    
    w5500_hardware_reset();
    
    uint8_t version = w5500_read_reg(W5500_VERSIONR, COMMON_REG_BSB);
    printf("[W5500] Version: 0x%02X\n", version);
    
    if (version != 0x04) {
        printf("[W5500] ❌ Invalid version!\n");
        update_oled_status("W5500 ERROR!");
        return false;
    }
    
    printf("[W5500] ✅ Init OK\n");
    return true;
}

static void w5500_setup_network(void) {
    printf("[W5500] Configuring network...\n");
    update_oled_status("Config network...");
    
    // Set MAC
    for (int i = 0; i < 6; i++) {
        w5500_write_reg(W5500_SHAR0 + i, COMMON_REG_BSB, MAC_ADDRESS[i]);
    }
    
    // Set Gateway
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_GAR0 + i, COMMON_REG_BSB, GATEWAY[i]);
    }
    
    // Set Subnet
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SUBR0 + i, COMMON_REG_BSB, SUBNET_MASK[i]);
    }
    
    // Set IP
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SIPR0 + i, COMMON_REG_BSB, IP_ADDRESS[i]);
    }
    
    printf("[W5500] IP: %d.%d.%d.%d\n",
           IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    
    // Check PHY
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("[W5500] Link: %s, %s, %s\n",
           (phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN",
           (phycfgr & PHYCFGR_SPD) ? "100M" : "10M",
           (phycfgr & PHYCFGR_DPX) ? "Full" : "Half");
}

static bool w5500_setup_tcp_server(uint16_t port) {
    printf("[W5500] Setting up TCP server on port %d\n", port);
    update_oled_status("Start HTTP server");
    
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
    sleep_ms(10);
    
    w5500_write_reg(S0_MR, SOCKET0_REG_BSB, SOCK_TCP);
    w5500_write_reg(S0_PORT0, SOCKET0_REG_BSB, (port >> 8) & 0xFF);
    w5500_write_reg(S0_PORT0 + 1, SOCKET0_REG_BSB, port & 0xFF);
    
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_OPEN);
    sleep_ms(10);
    
    uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
    if (status != SOCK_STAT_INIT) {
        printf("[W5500] ❌ Failed to open socket\n");
        update_oled_status("TCP open failed!");
        return false;
    }
    
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_LISTEN);
    sleep_ms(10);
    
    status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
    if (status != SOCK_STAT_LISTEN) {
        printf("[W5500] ❌ Failed to listen\n");
        update_oled_status("TCP listen failed!");
        return false;
    }
    
    printf("[W5500] ✅ Listening on port %d\n", port);
    update_oled_status("HTTP ready!");
    return true;
}

static bool w5500_send_response(const char* response, uint16_t length) {
    uint8_t tx_wr_high = w5500_read_reg(0x0024, SOCKET0_REG_BSB);
    uint8_t tx_wr_low = w5500_read_reg(0x0025, SOCKET0_REG_BSB);
    uint16_t tx_wr_ptr = (tx_wr_high << 8) | tx_wr_low;
    
    for (uint16_t i = 0; i < length; i++) {
        uint16_t addr = (tx_wr_ptr + i) & 0x07FF;
        w5500_write_reg(addr, 0x14, response[i]);
    }
    
    tx_wr_ptr += length;
    w5500_write_reg(0x0024, SOCKET0_REG_BSB, (tx_wr_ptr >> 8) & 0xFF);
    w5500_write_reg(0x0025, SOCKET0_REG_BSB, tx_wr_ptr & 0xFF);
    
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_SEND);
    
    int timeout = 100;
    uint8_t cmd_status;
    do {
        cmd_status = w5500_read_reg(S0_CR, SOCKET0_REG_BSB);
        sleep_ms(5);
        timeout--;
    } while (cmd_status != 0 && timeout > 0);
    
    return (timeout > 0);
}

// ============================================================================
// HTTP Request Handler (stub - will call z1_http_api.c functions)
// ============================================================================

static void handle_http_request(const char* request, size_t length) {
    printf("[HTTP] Request received (%zu bytes)\n", length);
    update_oled_status("HTTP request");
    http_connection_active = true;
    
    // Simple response for now - will integrate full API later
    const char* response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"status\":\"ok\",\"controller\":\"Z1\",\"nodes\":%d}\r\n";
    
    char buffer[512];
    snprintf(buffer, sizeof(buffer), response, active_node_count);
    
    w5500_send_response(buffer, strlen(buffer));
    
    // Update display based on request type
    if (strstr(request, "/api/snn/deploy")) {
        update_oled_status("SNN deploy request");
    } else if (strstr(request, "/api/snn/start")) {
        update_oled_status("SNN start");
    } else if (strstr(request, "/api/snn/stop")) {
        update_oled_status("SNN stop");
    } else if (strstr(request, "/api/nodes")) {
        update_oled_status("Node query");
    } else {
        update_oled_status("HTTP request OK");
    }
}

// ============================================================================
// Main HTTP Server Loop
// ============================================================================

static void monitor_connections(void) {
    printf("[HTTP] Monitoring connections...\n");
    update_oled_status("Monitoring...");
    
    char request_buffer[2048];
    uint32_t loop_count = 0;
    
    while (true) {
        uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        
        switch (status) {
            case SOCK_STAT_LISTEN:
                // Waiting for connection
                if (loop_count % 100 == 0) {
                    http_connection_active = false;
                    update_oled_status("Listening...");
                }
                break;
                
            case SOCK_STAT_ESTABLISHED:
                http_connection_active = true;
                
                // Check for received data
                uint8_t rx_size_high = w5500_read_reg(0x0026, SOCKET0_REG_BSB);
                uint8_t rx_size_low = w5500_read_reg(0x0027, SOCKET0_REG_BSB);
                uint16_t rx_size = (rx_size_high << 8) | rx_size_low;
                
                if (rx_size > 0) {
                    printf("[HTTP] Received %d bytes\n", rx_size);
                    update_oled_status("Processing...");
                    
                    // Read request
                    uint8_t rx_rd_high = w5500_read_reg(0x0028, SOCKET0_REG_BSB);
                    uint8_t rx_rd_low = w5500_read_reg(0x0029, SOCKET0_REG_BSB);
                    uint16_t rx_rd_ptr = (rx_rd_high << 8) | rx_rd_low;
                    
                    uint16_t read_size = (rx_size < sizeof(request_buffer) - 1) ? 
                                        rx_size : sizeof(request_buffer) - 1;
                    
                    for (uint16_t i = 0; i < read_size; i++) {
                        uint16_t addr = (rx_rd_ptr + i) & 0x07FF;
                        request_buffer[i] = w5500_read_reg(addr, 0x18);
                    }
                    request_buffer[read_size] = '\0';
                    
                    // Update read pointer
                    rx_rd_ptr += rx_size;
                    w5500_write_reg(0x0028, SOCKET0_REG_BSB, (rx_rd_ptr >> 8) & 0xFF);
                    w5500_write_reg(0x0029, SOCKET0_REG_BSB, rx_rd_ptr & 0xFF);
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_RECV);
                    
                    // Handle request
                    handle_http_request(request_buffer, read_size);
                    
                    // Close connection
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                    sleep_ms(50);
                }
                break;
                
            case SOCK_STAT_CLOSE_WAIT:
                printf("[HTTP] Connection closing\n");
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_CLOSED:
                printf("[HTTP] Reopening socket\n");
                w5500_setup_tcp_server(80);
                break;
        }
        
        // Heartbeat LED
        if (loop_count % 20 == 0) {
            set_led_color(0, 0, 100);
        } else if (loop_count % 20 == 1) {
            set_led_color(0, 0, 20);
        }
        
        sleep_ms(50);
        loop_count++;
    }
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
    printf("║  With W5500 HTTP Server & OLED Display                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Initialize LED
    printf("[Init] RGB LED...\n");
    init_led();
    set_led_color(0, 100, 0);  // Green
    
    // Initialize OLED
    printf("[Init] OLED Display...\n");
    ssd1306_init(OLED_I2C, OLED_SDA_PIN, OLED_SCL_PIN);
    oled_initialized = true;
    update_oled_status("Booting...");
    
    // Initialize PSRAM
    printf("[Init] PSRAM...\n");
    update_oled_status("Init PSRAM...");
    if (!psram_init()) {
        printf("[Init] ⚠️  PSRAM init failed\n");
        update_oled_status("PSRAM failed!");
    }
    
    // Initialize Matrix Bus
    printf("[Init] Matrix Bus (Node ID: %d)...\n", Z1_CONTROLLER_NODE_ID);
    update_oled_status("Init bus...");
    if (!z1_bus_init(Z1_CONTROLLER_NODE_ID)) {
        printf("[Init] ❌ Bus init failed!\n");
        update_oled_status("Bus failed!");
        set_led_color(255, 0, 0);
        while (1) sleep_ms(1000);
    }
    printf("[Init] ✅ Matrix bus OK\n");
    
    // Discover nodes
    printf("[Init] Discovering nodes...\n");
    update_oled_status("Scanning nodes...");
    bool active_nodes[16] = {false};
    z1_discover_nodes_sequential(active_nodes);
    
    active_node_count = 0;
    for (int i = 0; i < 16; i++) {
        if (active_nodes[i]) active_node_count++;
    }
    printf("[Init] ✅ Found %d nodes\n", active_node_count);
    
    char node_msg[32];
    snprintf(node_msg, sizeof(node_msg), "Found %d nodes", active_node_count);
    update_oled_status(node_msg);
    sleep_ms(1000);
    
    // Initialize W5500
    if (!w5500_init()) {
        printf("[Init] ❌ W5500 init failed!\n");
        update_oled_status("W5500 failed!");
        set_led_color(255, 0, 0);
        while (1) sleep_ms(1000);
    }
    
    w5500_setup_network();
    
    if (!w5500_setup_tcp_server(80)) {
        printf("[Init] ❌ TCP server failed!\n");
        update_oled_status("TCP failed!");
        set_led_color(255, 0, 0);
        while (1) sleep_ms(1000);
    }
    
    set_led_color(0, 0, 100);  // Blue = ready
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Controller Ready                                          ║\n");
    printf("║  HTTP Server: http://%d.%d.%d.%d                     ║\n",
           IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    printf("║  Nodes Active: %-3d                                         ║\n", active_node_count);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Start monitoring
    monitor_connections();
    
    return 0;
}
