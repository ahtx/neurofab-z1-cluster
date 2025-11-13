/**
 * W5500 HTTP Server - Complete Implementation
 * 
 * Full HTTP server with request parsing, routing, and response handling
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef W5500_HTTP_SERVER_H
#define W5500_HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "z1_http_api.h"

// Initialize W5500 hardware
bool w5500_init(void);

// Setup TCP server on specified port
bool w5500_setup_tcp_server(uint16_t port);

// Run HTTP server (blocking loop)
void w5500_http_server_run(void);

// Response functions are declared in z1_http_api.h

// W5500 register access (used internally and by API)
uint8_t w5500_read_reg(uint16_t addr, uint8_t bsb);
void w5500_write_reg(uint16_t addr, uint8_t bsb, uint8_t data);

#endif // W5500_HTTP_SERVER_H
