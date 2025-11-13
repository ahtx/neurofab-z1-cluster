/**
 * W5500 HTTP Server - Complete Implementation
 * 
 * Full HTTP server with request parsing, routing, and response handling
 * Routes requests to z1_http_api.c handlers
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "w5500_http_server.h"
#include "z1_http_api.h"
#include "z1_display.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// W5500 Pin Definitions
#define W5500_SPI_PORT spi0
#define W5500_SCK      38
#define W5500_MOSI     39
#define W5500_MISO     36
#define W5500_CS       37
#define W5500_RST      34
#define W5500_INT      35

// W5500 Registers
#define W5500_MR         0x0000
#define W5500_SHAR0      0x0009
#define W5500_GAR0       0x0001
#define W5500_SUBR0      0x0005
#define W5500_SIPR0      0x000F
#define W5500_VERSIONR   0x0039
#define W5500_PHYCFGR    0x002E

#define PHYCFGR_LNK      0x01
#define PHYCFGR_SPD      0x02
#define PHYCFGR_DPX      0x04

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

// HTTP Response Buffer
static char http_response_buffer[4096];

// ============================================================================
// W5500 Low-Level Functions
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

// ============================================================================
// W5500 Initialization
// ============================================================================

bool w5500_init(void) {
    printf("[W5500] Initializing...\n");
    z1_display_status("Init W5500...");
    
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
        z1_display_error("W5500 init failed");
        return false;
    }
    
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
    
    printf("[W5500] ✅ Init OK\n");
    return true;
}

// ============================================================================
// TCP Server Setup
// ============================================================================

bool w5500_setup_tcp_server(uint16_t port) {
    printf("[W5500] Setting up TCP server on port %d\n", port);
    z1_display_status("Start HTTP server");
    
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
        z1_display_error("TCP open failed");
        return false;
    }
    
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_LISTEN);
    sleep_ms(10);
    
    status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
    if (status != SOCK_STAT_LISTEN) {
        printf("[W5500] ❌ Failed to listen\n");
        z1_display_error("TCP listen failed");
        return false;
    }
    
    printf("[W5500] ✅ Listening on port %d\n", port);
    z1_display_status("HTTP ready!");
    return true;
}

// ============================================================================
// HTTP Response Sending
// ============================================================================

static bool w5500_send_data(const char* data, uint16_t length) {
    uint8_t tx_wr_high = w5500_read_reg(0x0024, SOCKET0_REG_BSB);
    uint8_t tx_wr_low = w5500_read_reg(0x0025, SOCKET0_REG_BSB);
    uint16_t tx_wr_ptr = (tx_wr_high << 8) | tx_wr_low;
    
    for (uint16_t i = 0; i < length; i++) {
        uint16_t addr = (tx_wr_ptr + i) & 0x07FF;
        w5500_write_reg(addr, 0x14, data[i]);
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

// HTTP response functions are implemented in z1_http_api.c
// We just provide the low-level W5500 send function

// Low-level send function used by z1_http_api.c
bool w5500_send_http_data(const char* data, uint16_t length) {
    return w5500_send_data(data, length);
}

// ============================================================================
// HTTP Request Parsing and Routing
// ============================================================================

static void route_http_request(http_connection_t* conn, const char* method, 
                               const char* path, const char* body, uint16_t body_length) {
    printf("[HTTP] %s %s\n", method, path);
    z1_display_http_request(method, path);
    
    // GET /api/nodes - List all nodes
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/nodes") == 0) {
        handle_get_nodes(conn);
        return;
    }
    
    // POST /api/nodes/discover - Discover nodes
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/nodes/discover") == 0) {
        handle_post_nodes_discover(conn);
        return;
    }
    
    // GET /api/snn/status - Get SNN status
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/snn/status") == 0) {
        handle_get_snn_status(conn);
        return;
    }
    
    // POST /api/snn/start - Start SNN
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/snn/start") == 0) {
        handle_post_snn_start(conn);
        return;
    }
    
    // POST /api/snn/stop - Stop SNN
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/snn/stop") == 0) {
        handle_post_snn_stop(conn);
        return;
    }
    
    // POST /api/snn/deploy - Deploy SNN
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/snn/deploy") == 0) {
        handle_post_snn_deploy(conn, body, body_length);
        return;
    }
    
    // POST /api/snn/input - Inject spikes
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/snn/input") == 0) {
        handle_post_snn_inject(conn, body, body_length);
        return;
    }
    
    // Parse paths with parameters
    char path_copy[128];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    // GET /api/nodes/{id} - Get specific node
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/nodes/", 11) == 0) {
        int node_id = atoi(path + 11);
        if (node_id >= 0 && node_id <= 15) {
            handle_get_node(conn, node_id);
            return;
        }
    }
    
    // POST /api/nodes/{id}/ping - Ping node
    if (strcmp(method, "POST") == 0 && strstr(path, "/ping") != NULL) {
        char* id_start = (char*)path + 11;  // After "/api/nodes/"
        int node_id = atoi(id_start);
        if (node_id >= 0 && node_id <= 15) {
            handle_post_node_ping(conn, node_id);
            return;
        }
    }
    
    // POST /api/nodes/{id}/reset - Reset node
    if (strcmp(method, "POST") == 0 && strstr(path, "/reset") != NULL) {
        char* id_start = (char*)path + 11;
        int node_id = atoi(id_start);
        if (node_id >= 0 && node_id <= 15) {
            handle_post_node_reset(conn, node_id);
            return;
        }
    }
    
    // Default: 404 Not Found
    z1_http_send_error(conn, 404, "Endpoint not found");
}

static void parse_and_route_request(const char* request, uint16_t length) {
    http_connection_t conn = {0};
    
    // Parse request line: "METHOD /path HTTP/1.1"
    char method[16] = {0};
    char path[128] = {0};
    
    const char* line_end = strstr(request, "\r\n");
    if (!line_end) {
        z1_http_send_error(&conn, 400, "Invalid request");
        return;
    }
    
    // Extract method
    const char* space1 = strchr(request, ' ');
    if (!space1 || space1 > line_end) {
        z1_http_send_error(&conn, 400, "Invalid request");
        return;
    }
    
    size_t method_len = space1 - request;
    if (method_len >= sizeof(method)) method_len = sizeof(method) - 1;
    strncpy(method, request, method_len);
    method[method_len] = '\0';
    
    // Extract path
    const char* path_start = space1 + 1;
    const char* space2 = strchr(path_start, ' ');
    if (!space2 || space2 > line_end) {
        z1_http_send_error(&conn, 400, "Invalid request");
        return;
    }
    
    size_t path_len = space2 - path_start;
    if (path_len >= sizeof(path)) path_len = sizeof(path) - 1;
    strncpy(path, path_start, path_len);
    path[path_len] = '\0';
    
    // Find body (after "\r\n\r\n")
    const char* body_start = strstr(request, "\r\n\r\n");
    const char* body = NULL;
    uint16_t body_length = 0;
    
    if (body_start) {
        body = body_start + 4;
        body_length = length - (body - request);
    }
    
    // Route request
    route_http_request(&conn, method, path, body, body_length);
}

// ============================================================================
// Main HTTP Server Loop
// ============================================================================

void w5500_http_server_run(void) {
    printf("[HTTP] Server running...\n");
    z1_display_status("HTTP listening");
    
    char request_buffer[2048];
    
    while (true) {
        uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        
        switch (status) {
            case SOCK_STAT_LISTEN:
                // Waiting for connection
                break;
                
            case SOCK_STAT_ESTABLISHED: {
                // Check for received data
                uint8_t rx_size_high = w5500_read_reg(0x0026, SOCKET0_REG_BSB);
                uint8_t rx_size_low = w5500_read_reg(0x0027, SOCKET0_REG_BSB);
                uint16_t rx_size = (rx_size_high << 8) | rx_size_low;
                
                if (rx_size > 0) {
                    printf("[HTTP] Received %d bytes\n", rx_size);
                    
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
                    
                    // Parse and route request
                    parse_and_route_request(request_buffer, read_size);
                    
                    // Close connection
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                    sleep_ms(50);
                }
                break;
            }
                
            case SOCK_STAT_CLOSE_WAIT:
                printf("[HTTP] Connection closing\n");
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_CLOSED:
                printf("[HTTP] Reopening socket\n");
                w5500_setup_tcp_server(80);
                break;
        }
        
        sleep_ms(10);
    }
}
