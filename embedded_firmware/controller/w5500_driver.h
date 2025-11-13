/**
 * W5500 Ethernet Controller Driver
 * 
 * Hardware SPI-based Ethernet for RP2350B controller
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// W5500 Pin Definitions
#define W5500_SPI_PORT spi0
#define W5500_SCK      38
#define W5500_MOSI     39
#define W5500_MISO     36
#define W5500_CS       37
#define W5500_RST      34
#define W5500_INT      35

// W5500 Register Definitions
#define W5500_MR         0x0000  // Mode Register
#define W5500_SHAR0      0x0009  // Source Hardware Address Register
#define W5500_GAR0       0x0001  // Gateway Address Register
#define W5500_SUBR0      0x0005  // Subnet Mask Register
#define W5500_SIPR0      0x000F  // Source IP Register
#define W5500_VERSIONR   0x0039  // Version Register
#define W5500_PHYCFGR    0x002E  // PHY Configuration Register

// PHY Configuration Values
#define PHYCFGR_RST      0x80    // PHY reset
#define PHYCFGR_OPMD     0x40    // Operation mode
#define PHYCFGR_OPMDC    0x38    // Operation mode configuration
#define PHYCFGR_DPX      0x04    // Duplex (1=full, 0=half)
#define PHYCFGR_SPD      0x02    // Speed (1=100Mbps, 0=10Mbps)
#define PHYCFGR_LNK      0x01    // Link status (1=up, 0=down)

// Socket 0 Registers
#define S0_MR            0x0000  // Socket Mode Register
#define S0_CR            0x0001  // Socket Command Register
#define S0_IR            0x0002  // Socket Interrupt Register
#define S0_SR            0x0003  // Socket Status Register
#define S0_PORT0         0x0004  // Socket Source Port Register

// Block Select Bits (BSB)
#define COMMON_REG_BSB   0x00    // Common register block
#define SOCKET0_REG_BSB  0x08    // Socket 0 register block

// Socket Commands
#define SOCK_OPEN        0x01
#define SOCK_LISTEN      0x02
#define SOCK_CONNECT     0x04
#define SOCK_DISCON      0x08
#define SOCK_CLOSE       0x10
#define SOCK_SEND        0x20
#define SOCK_RECV        0x40

// Socket Status Values
#define SOCK_STAT_CLOSED      0x00
#define SOCK_STAT_INIT        0x13
#define SOCK_STAT_LISTEN      0x14
#define SOCK_STAT_SYNSENT     0x15
#define SOCK_STAT_SYNRECV     0x16
#define SOCK_STAT_ESTABLISHED 0x17
#define SOCK_STAT_FIN_WAIT    0x18
#define SOCK_STAT_CLOSING     0x1A
#define SOCK_STAT_TIME_WAIT   0x1B
#define SOCK_STAT_CLOSE_WAIT  0x1C
#define SOCK_STAT_LAST_ACK    0x1D

// Socket Modes
#define SOCK_TCP         0x01

// Network Configuration
extern const uint8_t MAC_ADDRESS[6];
extern const uint8_t IP_ADDRESS[4];
extern const uint8_t GATEWAY[4];
extern const uint8_t SUBNET_MASK[4];

// W5500 Functions
bool w5500_init(void);
uint8_t w5500_read_reg(uint16_t addr, uint8_t bsb);
void w5500_write_reg(uint16_t addr, uint8_t bsb, uint8_t data);
void w5500_hardware_reset(void);
void w5500_setup_network(void);
bool w5500_setup_tcp_server(uint16_t port);
void w5500_monitor_connections(void);
bool w5500_send_data(const char* data, uint16_t length);
const char* w5500_get_socket_status_name(uint8_t status);

#endif // W5500_DRIVER_H
