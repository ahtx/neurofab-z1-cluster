#ifndef PSRAM_RP2350_H
#define PSRAM_RP2350_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file psram_rp2350.h
 * @brief PSRAM Library for RP2350 Microcontroller
 * 
 * This library provides a clean interface for PSRAM initialization and usage
 * on the RP2350 microcontroller using the QMI (Quad SPI Memory Interface).
 * 
 * @section Usage Example
 * @code
 * #include "psram_rp2350.h"
 * 
 * int main() {
 *     stdio_init_all();
 *     
 *     // Initialize PSRAM
 *     if (psram_init()) {
 *         printf("PSRAM initialized: %zu MB\n", psram_get_size() / (1024*1024));
 *         
 *         // Get a pointer to PSRAM memory
 *         volatile uint8_t *psram_ptr = psram_get_pointer(0);
 *         if (psram_ptr) {
 *             // Write to PSRAM
 *             *psram_ptr = 0x42;
 *             
 *             // Test memory
 *             if (psram_test_memory(psram_get_base_address())) {
 *                 printf("PSRAM test passed!\n");
 *             }
 *         }
 *     } else {
 *         printf("PSRAM initialization failed!\n");
 *     }
 *     
 *     return 0;
 * }
 * @endcode
 */

// PSRAM Configuration
#define PSRAM_CS_PIN          47      // GPIO47 for PSRAM CS
#define PSRAM_BASE_ADDRESS    0x11000000  // Fixed RP2350 PSRAM address

// PSRAM Commands
#define PSRAM_CMD_QUAD_END    0xF5
#define PSRAM_CMD_QUAD_ENABLE 0x35
#define PSRAM_CMD_READ_ID     0x9F
#define PSRAM_CMD_QUAD_READ   0xEB
#define PSRAM_CMD_QUAD_WRITE  0x38
#define PSRAM_CMD_NOOP        0xFF
#define PSRAM_ID              0x5D

/**
 * @brief Initialize PSRAM on RP2350
 * 
 * Sets up the QMI interface for PSRAM communication, detects PSRAM size,
 * configures quad mode if possible, and tests connectivity.
 * 
 * @return true if PSRAM initialization successful, false otherwise
 */
bool psram_init(void);

/**
 * @brief Get PSRAM size in bytes
 * 
 * @return Size of PSRAM in bytes, or 0 if not initialized or detection failed
 */
size_t psram_get_size(void);

/**
 * @brief Get PSRAM base address
 * 
 * @return Base address of PSRAM memory region
 */
uint32_t psram_get_base_address(void);

/**
 * @brief Check if PSRAM is initialized
 * 
 * @return true if PSRAM is initialized and ready, false otherwise
 */
bool psram_is_initialized(void);

/**
 * @brief Check if PSRAM is running in quad mode
 * 
 * @return true if quad mode is active, false if single-line mode
 */
bool psram_is_quad_mode(void);

/**
 * @brief Test PSRAM memory with simple write/read pattern
 * 
 * Performs a basic connectivity test by writing and reading back a test pattern.
 * 
 * @param test_address Address to test (must be within PSRAM range)
 * @return true if test passes, false if test fails
 */
bool psram_test_memory(uint32_t test_address);

/**
 * @brief Get a pointer to PSRAM memory
 * 
 * @param offset Offset from PSRAM base address
 * @return Pointer to PSRAM memory at the specified offset, or NULL if not initialized
 */
volatile uint8_t* psram_get_pointer(size_t offset);

/**
 * @brief Read data from PSRAM
 * 
 * @param address PSRAM address to read from
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return true if read successful, false otherwise
 */
bool psram_read(uint32_t address, void* buffer, size_t length);

/**
 * @brief Write data to PSRAM
 * 
 * @param address PSRAM address to write to
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return true if write successful, false otherwise
 */
bool psram_write(uint32_t address, const void* buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif // PSRAM_RP2350_H
