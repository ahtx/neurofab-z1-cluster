/**
 * W5500 Ethernet Controller Driver - Simplified Implementation
 * 
 * Hardware SPI-based Ethernet for RP2350B controller
 * Focused on HTTP server for SNN API endpoints
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "w5500_driver.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

// Network Configuration
const uint8_t MAC_ADDRESS[6] = {0x02, 0x08, 0xDC, 0x00, 0x00, 0x01};
const uint8_t IP_ADDRESS[4]  = {192, 168, 1, 222};
const uint8_t GATEWAY[4]     = {192, 168, 1, 254};
const uint8_t SUBNET_MASK[4] = {255, 255, 255, 0};

// W5500 SPI Control
static void w5500_select(void) {
    gpio_put(W5500_CS, 0);
    sleep_us(1);
}

static void w5500_deselect(void) {
    sleep_us(1);
    gpio_put(W5500_CS, 1);
    sleep_us(1);
}

// Read W5500 register
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

// Write W5500 register
void w5500_write_reg(uint16_t addr, uint8_t bsb, uint8_t data) {
    w5500_select();
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr >> 8}, 1);
    spi_write_blocking(W5500_SPI_PORT, (uint8_t[]){addr & 0xFF}, 1);
    uint8_t control = bsb | 0x04;
    spi_write_blocking(W5500_SPI_PORT, &control, 1);
    spi_write_blocking(W5500_SPI_PORT, &data, 1);
    w5500_deselect();
}

// Hardware reset W5500
void w5500_hardware_reset(void) {
    printf("[W5500] Hardware reset...\n");
    gpio_put(W5500_RST, 0);
    sleep_ms(10);
    gpio_put(W5500_RST, 1);
    sleep_ms(200);
}

// Initialize W5500 hardware
bool w5500_init(void) {
    printf("[W5500] Initializing W5500 Ethernet...\n");
    
    // Initialize SPI
    spi_init(W5500_SPI_PORT, 40000000);  // 40MHz
    gpio_set_function(W5500_SCK, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MISO, GPIO_FUNC_SPI);
    
    // Initialize CS pin
    gpio_init(W5500_CS);
    gpio_set_dir(W5500_CS, GPIO_OUT);
    gpio_put(W5500_CS, 1);
    
    // Initialize RST pin
    gpio_init(W5500_RST);
    gpio_set_dir(W5500_RST, GPIO_OUT);
    gpio_put(W5500_RST, 1);
    
    // Hardware reset
    w5500_hardware_reset();
    
    // Check version
    uint8_t version = w5500_read_reg(W5500_VERSIONR, COMMON_REG_BSB);
    printf("[W5500] Chip version: 0x%02X\n", version);
    
    if (version != 0x04) {
        printf("[W5500] ❌ Invalid version! Expected 0x04\n");
        return false;
    }
    
    printf("[W5500] ✅ Initialization successful\n");
    return true;
}

// Setup network configuration
void w5500_setup_network(void) {
    printf("[W5500] Configuring network...\n");
    
    // Set MAC address
    for (int i = 0; i < 6; i++) {
        w5500_write_reg(W5500_SHAR0 + i, COMMON_REG_BSB, MAC_ADDRESS[i]);
    }
    
    // Set Gateway
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_GAR0 + i, COMMON_REG_BSB, GATEWAY[i]);
    }
    
    // Set Subnet Mask
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SUBR0 + i, COMMON_REG_BSB, SUBNET_MASK[i]);
    }
    
    // Set IP Address
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SIPR0 + i, COMMON_REG_BSB, IP_ADDRESS[i]);
    }
    
    printf("[W5500] Network configured:\n");
    printf("        MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           MAC_ADDRESS[0], MAC_ADDRESS[1], MAC_ADDRESS[2],
           MAC_ADDRESS[3], MAC_ADDRESS[4], MAC_ADDRESS[5]);
    printf("        IP:  %d.%d.%d.%d\n",
           IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    printf("        GW:  %d.%d.%d.%d\n",
           GATEWAY[0], GATEWAY[1], GATEWAY[2], GATEWAY[3]);
    printf("        Mask: %d.%d.%d.%d\n",
           SUBNET_MASK[0], SUBNET_MASK[1], SUBNET_MASK[2], SUBNET_MASK[3]);
    
    // Check PHY link status
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("[W5500] PHY Status: Link %s, %s, %s duplex\n",
           (phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN",
           (phycfgr & PHYCFGR_SPD) ? "100Mbps" : "10Mbps",
           (phycfgr & PHYCFGR_DPX) ? "Full" : "Half");
}

// Setup TCP server on specified port
bool w5500_setup_tcp_server(uint16_t port) {
    printf("[W5500] Setting up TCP server on port %d...\n", port);
    
    // Close socket if open
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
    sleep_ms(10);
    
    // Set socket mode to TCP
    w5500_write_reg(S0_MR, SOCKET0_REG_BSB, SOCK_TCP);
    
    // Set source port
    w5500_write_reg(S0_PORT0, SOCKET0_REG_BSB, (port >> 8) & 0xFF);
    w5500_write_reg(S0_PORT0 + 1, SOCKET0_REG_BSB, port & 0xFF);
    
    // Open socket
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_OPEN);
    sleep_ms(10);
    
    // Check if socket opened
    uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
    if (status != SOCK_STAT_INIT) {
        printf("[W5500] ❌ Failed to open socket (status: 0x%02X)\n", status);
        return false;
    }
    
    // Start listening
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_LISTEN);
    sleep_ms(10);
    
    status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
    if (status != SOCK_STAT_LISTEN) {
        printf("[W5500] ❌ Failed to listen (status: 0x%02X)\n", status);
        return false;
    }
    
    printf("[W5500] ✅ TCP server listening on port %d\n", port);
    return true;
}

// Send data over TCP
bool w5500_send_data(const char* data, uint16_t length) {
    // Get current TX write pointer
    uint8_t tx_wr_high = w5500_read_reg(0x0024, SOCKET0_REG_BSB);
    uint8_t tx_wr_low = w5500_read_reg(0x0025, SOCKET0_REG_BSB);
    uint16_t tx_wr_ptr = (tx_wr_high << 8) | tx_wr_low;
    
    // Write data to TX buffer
    for (uint16_t i = 0; i < length; i++) {
        uint16_t addr = (tx_wr_ptr + i) & 0x07FF;
        w5500_write_reg(addr, 0x14, data[i]);
    }
    
    // Update TX write pointer
    tx_wr_ptr += length;
    w5500_write_reg(0x0024, SOCKET0_REG_BSB, (tx_wr_ptr >> 8) & 0xFF);
    w5500_write_reg(0x0025, SOCKET0_REG_BSB, tx_wr_ptr & 0xFF);
    
    // Send command
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_SEND);
    
    // Wait for send to complete
    int timeout = 100;
    uint8_t cmd_status;
    do {
        cmd_status = w5500_read_reg(S0_CR, SOCKET0_REG_BSB);
        sleep_ms(5);
        timeout--;
    } while (cmd_status != 0 && timeout > 0);
    
    return (timeout > 0);
}

// Get socket status name
const char* w5500_get_socket_status_name(uint8_t status) {
    switch (status) {
        case SOCK_STAT_CLOSED:      return "CLOSED";
        case SOCK_STAT_INIT:        return "INIT";
        case SOCK_STAT_LISTEN:      return "LISTEN";
        case SOCK_STAT_SYNSENT:     return "SYNSENT";
        case SOCK_STAT_SYNRECV:     return "SYNRECV";
        case SOCK_STAT_ESTABLISHED: return "ESTABLISHED";
        case SOCK_STAT_FIN_WAIT:    return "FIN_WAIT";
        case SOCK_STAT_CLOSING:     return "CLOSING";
        case SOCK_STAT_TIME_WAIT:   return "TIME_WAIT";
        case SOCK_STAT_CLOSE_WAIT:  return "CLOSE_WAIT";
        case SOCK_STAT_LAST_ACK:    return "LAST_ACK";
        default:                     return "UNKNOWN";
    }
}
