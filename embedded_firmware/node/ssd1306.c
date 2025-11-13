/**
 * SSD1306 OLED Display Driver
 * 
 * 128x64 monochrome OLED display via I2C
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// SSD1306 configuration
#define SSD1306_I2C_INSTANCE i2c0
#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

// I2C pins
#define SSD1306_SDA_PIN 4
#define SSD1306_SCL_PIN 5

static bool ssd1306_initialized = false;

/**
 * Send command to SSD1306
 */
static void ssd1306_send_command(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(SSD1306_I2C_INSTANCE, SSD1306_I2C_ADDR, buf, 2, false);
}

/**
 * Initialize SSD1306
 */
bool ssd1306_init(void) {
    if (ssd1306_initialized) {
        return true;
    }
    
    printf("Initializing SSD1306 OLED...\n");
    
    // Initialize I2C
    i2c_init(SSD1306_I2C_INSTANCE, 400 * 1000); // 400 kHz
    gpio_set_function(SSD1306_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SSD1306_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SSD1306_SDA_PIN);
    gpio_pull_up(SSD1306_SCL_PIN);
    
    sleep_ms(100);
    
    // Initialize display
    ssd1306_send_command(0xAE); // Display off
    ssd1306_send_command(0xD5); // Set display clock
    ssd1306_send_command(0x80);
    ssd1306_send_command(0xA8); // Set multiplex
    ssd1306_send_command(0x3F);
    ssd1306_send_command(0xD3); // Set display offset
    ssd1306_send_command(0x00);
    ssd1306_send_command(0x40); // Set start line
    ssd1306_send_command(0x8D); // Charge pump
    ssd1306_send_command(0x14);
    ssd1306_send_command(0x20); // Memory mode
    ssd1306_send_command(0x00);
    ssd1306_send_command(0xA1); // Segment remap
    ssd1306_send_command(0xC8); // COM scan direction
    ssd1306_send_command(0xDA); // COM pins
    ssd1306_send_command(0x12);
    ssd1306_send_command(0x81); // Contrast
    ssd1306_send_command(0xCF);
    ssd1306_send_command(0xD9); // Pre-charge
    ssd1306_send_command(0xF1);
    ssd1306_send_command(0xDB); // VCOMH
    ssd1306_send_command(0x40);
    ssd1306_send_command(0xA4); // Display all on resume
    ssd1306_send_command(0xA6); // Normal display
    ssd1306_send_command(0xAF); // Display on
    
    ssd1306_initialized = true;
    printf("SSD1306 initialized\n");
    
    return true;
}

/**
 * Clear display
 */
void ssd1306_clear(void) {
    if (!ssd1306_initialized) {
        return;
    }
    
    uint8_t buf[SSD1306_WIDTH + 1];
    buf[0] = 0x40; // Data mode
    memset(buf + 1, 0, SSD1306_WIDTH);
    
    for (int page = 0; page < 8; page++) {
        ssd1306_send_command(0xB0 + page); // Set page
        ssd1306_send_command(0x00); // Set column low
        ssd1306_send_command(0x10); // Set column high
        i2c_write_blocking(SSD1306_I2C_INSTANCE, SSD1306_I2C_ADDR, buf, SSD1306_WIDTH + 1, false);
    }
}

/**
 * Display text (stub)
 */
void ssd1306_display_text(const char *text, int x, int y) {
    // TODO: Implement text rendering
    (void)text;
    (void)x;
    (void)y;
}

/**
 * Update display
 */
void ssd1306_update(void) {
    // Display is updated immediately in this simple implementation
}
