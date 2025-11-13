/**
 * PSRAM Driver for RP2350B
 * 
 * Supports 16MB QSPI PSRAM (APS6404L-3SQR)
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

// PSRAM configuration
#define PSRAM_SIZE_MB 16
#define PSRAM_SIZE_BYTES (PSRAM_SIZE_MB * 1024 * 1024)
#define PSRAM_SPI_INSTANCE spi0
#define PSRAM_CS_PIN 17
#define PSRAM_SCK_PIN 18
#define PSRAM_MOSI_PIN 19
#define PSRAM_MISO_PIN 16

// PSRAM commands
#define PSRAM_CMD_READ 0x03
#define PSRAM_CMD_WRITE 0x02
#define PSRAM_CMD_RESET_ENABLE 0x66
#define PSRAM_CMD_RESET 0x99

static bool psram_initialized = false;

/**
 * Initialize PSRAM
 */
bool psram_init(void) {
    if (psram_initialized) {
        return true;
    }
    
    printf("Initializing PSRAM...\n");
    
    // Initialize SPI
    spi_init(PSRAM_SPI_INSTANCE, 1000 * 1000); // 1 MHz initially
    
    // Configure GPIO pins
    gpio_set_function(PSRAM_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PSRAM_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PSRAM_MISO_PIN, GPIO_FUNC_SPI);
    
    // CS pin as GPIO output
    gpio_init(PSRAM_CS_PIN);
    gpio_set_dir(PSRAM_CS_PIN, GPIO_OUT);
    gpio_put(PSRAM_CS_PIN, 1); // Deselect
    
    // Reset PSRAM
    gpio_put(PSRAM_CS_PIN, 0);
    uint8_t cmd = PSRAM_CMD_RESET_ENABLE;
    spi_write_blocking(PSRAM_SPI_INSTANCE, &cmd, 1);
    gpio_put(PSRAM_CS_PIN, 1);
    sleep_us(10);
    
    gpio_put(PSRAM_CS_PIN, 0);
    cmd = PSRAM_CMD_RESET;
    spi_write_blocking(PSRAM_SPI_INSTANCE, &cmd, 1);
    gpio_put(PSRAM_CS_PIN, 1);
    sleep_ms(1);
    
    // Increase SPI speed to 33 MHz
    spi_set_baudrate(PSRAM_SPI_INSTANCE, 33 * 1000 * 1000);
    
    psram_initialized = true;
    printf("PSRAM initialized: %d MB\n", PSRAM_SIZE_MB);
    
    return true;
}

/**
 * Read from PSRAM
 */
bool psram_read(uint32_t address, uint8_t *buffer, size_t length) {
    if (!psram_initialized || address + length > PSRAM_SIZE_BYTES) {
        return false;
    }
    
    gpio_put(PSRAM_CS_PIN, 0);
    
    // Send read command and address
    uint8_t cmd[4] = {
        PSRAM_CMD_READ,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };
    spi_write_blocking(PSRAM_SPI_INSTANCE, cmd, 4);
    
    // Read data
    spi_read_blocking(PSRAM_SPI_INSTANCE, 0, buffer, length);
    
    gpio_put(PSRAM_CS_PIN, 1);
    
    return true;
}

/**
 * Write to PSRAM
 */
bool psram_write(uint32_t address, const uint8_t *buffer, size_t length) {
    if (!psram_initialized || address + length > PSRAM_SIZE_BYTES) {
        return false;
    }
    
    gpio_put(PSRAM_CS_PIN, 0);
    
    // Send write command and address
    uint8_t cmd[4] = {
        PSRAM_CMD_WRITE,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };
    spi_write_blocking(PSRAM_SPI_INSTANCE, cmd, 4);
    
    // Write data
    spi_write_blocking(PSRAM_SPI_INSTANCE, buffer, length);
    
    gpio_put(PSRAM_CS_PIN, 1);
    
    return true;
}

/**
 * Get PSRAM size
 */
uint32_t psram_get_size(void) {
    return PSRAM_SIZE_BYTES;
}

/**
 * Test PSRAM
 */
bool psram_test(void) {
    if (!psram_initialized) {
        return false;
    }
    
    printf("Testing PSRAM...\n");
    
    // Write test pattern
    uint8_t test_data[256];
    for (int i = 0; i < 256; i++) {
        test_data[i] = i;
    }
    
    if (!psram_write(0, test_data, 256)) {
        printf("PSRAM write failed\n");
        return false;
    }
    
    // Read back
    uint8_t read_data[256];
    if (!psram_read(0, read_data, 256)) {
        printf("PSRAM read failed\n");
        return false;
    }
    
    // Verify
    for (int i = 0; i < 256; i++) {
        if (read_data[i] != test_data[i]) {
            printf("PSRAM verify failed at offset %d\n", i);
            return false;
        }
    }
    
    printf("PSRAM test passed\n");
    return true;
}
