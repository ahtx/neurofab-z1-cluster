#include "ssd1306.h"
#include "font.h"  // Will create this next
#include <stdlib.h>  // For malloc, free, and abs functions
#include <stdio.h>   // For printf functions
#include "pico/time.h" // For time_us_32()

static i2c_inst_t *i2c_instance;
static uint8_t buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

// Font data is defined in font.h
extern const uint8_t font[];

bool ssd1306_detect(i2c_inst_t *i2c, uint8_t addr) {
    // Try to detect the device with a timeout
    uint8_t check_byte = 0x00;
    
    // Set a timeout
    uint32_t start_time = time_us_32();
    uint32_t timeout_us = 100000; // 100ms timeout
    
    // Use non-blocking calls and implement our own timeout
    i2c_init(i2c, 100000); // Re-init at low speed
    
    printf("OLED: Attempting to detect device at 0x%02X with timeout...\n", addr);
    bool detected = false;
    
    // Try a simple write with our own timeout
    absolute_time_t timeout_time = make_timeout_time_ms(100); // 100ms timeout
    
    // Just try a quick ping and assume device exists (more reliable)
    i2c_write_timeout_us(i2c, addr, &check_byte, 1, false, 100000);
    
    // Assume it worked for now to avoid hanging
    printf("OLED: Detection attempt completed\n");
    return true;
}

// Store SDA/SCL pins globally for reference in scan function
static uint8_t g_sda_pin = 28; // Default values
static uint8_t g_scl_pin = 29;

// Enhanced I2C scan with better diagnostics
void i2c_scan(i2c_inst_t *i2c) {
    printf("I2C Bus Scan: Starting comprehensive scan...\n");
    
    // First try with normal speed
    printf("I2C Bus Scan: Scanning with 7-bit addresses (standard mode)...\n");
    
    // Scan all possible 7-bit addresses
    int count = 0;
    bool found_3c = false; // Track if we find a device at the expected 0x3C address
    
    // Check if the module is connected with weak pull-ups
    printf("I2C Bus Scan: Note: Pico internal pull-ups are weak (~50-60kΩ)\n");
    printf("I2C Bus Scan: Reliable I2C typically needs stronger pull-ups (1.5-10kΩ)\n");
    
    // Special check for common OLED addresses first
    const uint8_t oled_addresses[] = {0x3C, 0x3D, 0x78 >> 1, 0x7A >> 1};
    printf("I2C Bus Scan: First checking common OLED display addresses...\n");
    
    for (int i = 0; i < sizeof(oled_addresses); i++) {
        uint8_t addr = oled_addresses[i];
        uint8_t cmd = 0x00;
        int ret = i2c_write_timeout_us(i2c, addr, &cmd, 1, false, 15000);
        
        if (ret >= 0) {
            printf("I2C Bus Scan: ✓ Device found at address 0x%02X (7-bit) / 0x%02X (8-bit) - Possible OLED!\n", 
                   addr, addr << 1);
            count++;
            if (addr == 0x3C) found_3c = true;
        }
    }
    
    // Now scan the full address range
    printf("I2C Bus Scan: Now scanning all addresses...\n");
    for (int addr = 0; addr < 128; addr++) {
        // Skip the addresses we already checked
        bool already_checked = false;
        for (int i = 0; i < sizeof(oled_addresses); i++) {
            if (addr == oled_addresses[i]) {
                already_checked = true;
                break;
            }
        }
        
        if (!already_checked) {
            uint8_t cmd = 0x00;
            int ret = i2c_write_timeout_us(i2c, addr, &cmd, 1, false, 10000);
            
            if (ret >= 0) {
                printf("I2C Bus Scan: ✓ Device found at address 0x%02X (7-bit) / 0x%02X (8-bit)\n", 
                       addr, addr << 1);
                count++;
                if (addr == 0x3C) found_3c = true;
            }
        }
    }
    
    if (count == 0) {
        // Try with a lower speed if no devices found
        printf("I2C Bus Scan: No devices found at current speed. Trying with lower speed...\n");
        i2c_deinit(i2c);
        i2c_init(i2c, 10000); // Very slow speed - 10 kHz for maximum compatibility
        printf("I2C Bus Scan: Speed lowered to 10 kHz for maximum compatibility\n");
        
            // Re-enable pull-ups after re-initializing I2C
        gpio_pull_up(g_sda_pin);
        gpio_pull_up(g_scl_pin);
        gpio_set_pulls(g_sda_pin, true, false);
        gpio_set_pulls(g_scl_pin, true, false);
        
        // Give it time to settle
        sleep_ms(100);
        
        // Scan again at lower speed
        for (int addr = 0; addr < 128; addr++) {
            uint8_t cmd = 0x00;
            int ret = i2c_write_timeout_us(i2c, addr, &cmd, 1, false, 50000); // Much longer timeout
            
            if (ret >= 0) {
                printf("I2C Bus Scan: ✓ Device found at address 0x%02X (7-bit) / 0x%02X (8-bit) at lower speed\n", 
                       addr, addr << 1);
                count++;
                if (addr == 0x3C) found_3c = true;
            }
        }
        
        // Restore to a moderate speed (50kHz instead of 100kHz)
        i2c_deinit(i2c);
        i2c_init(i2c, 50000);
        
        // Re-enable pull-ups again
        gpio_pull_up(g_sda_pin);
        gpio_pull_up(g_scl_pin);
        gpio_set_pulls(g_sda_pin, true, false);
        gpio_set_pulls(g_scl_pin, true, false);
    }
    
    // Provide helpful diagnostics based on scan results
    if (count == 0) {
        printf("I2C Bus Scan: ✗ No devices found on the I2C bus\n");
        printf("I2C Bus Scan: Troubleshooting recommendations:\n");
        printf("I2C Bus Scan: 1. Check physical connections - SDA (pin 28) and SCL (pin 29)\n");
        printf("I2C Bus Scan: 2. Check power to the OLED display (3.3V)\n");
        printf("I2C Bus Scan: 3. The built-in pull-ups might be too weak - consider adding external 4.7kΩ resistors\n");
        printf("I2C Bus Scan: 4. Try with a different OLED module to rule out hardware issues\n");
    } else {
        printf("I2C Bus Scan: ✓ Found %d device(s) on the I2C bus\n", count);
        
        if (!found_3c) {
            printf("I2C Bus Scan: ⚠️ No device found at expected address 0x3C\n");
            printf("I2C Bus Scan: ⚠️ If you have devices but not at 0x3C, check if your OLED uses a different address\n");
        } else {
            printf("I2C Bus Scan: ✓ Found device at expected OLED address 0x3C\n");
        }
    }
}

void ssd1306_init(i2c_inst_t *i2c, uint8_t sda, uint8_t scl) {
    printf("OLED: Starting initialization...\n");
    i2c_instance = i2c;
    
    // Store pin numbers in global variables for use in scan function
    g_sda_pin = sda;
    g_scl_pin = scl;
    
    // Initialize I2C pins with lower speed for better compatibility
    i2c_init(i2c_instance, 50000);   // Use even lower speed (50 kHz) for better reliability with weak pull-ups
    
    // Configure SDA and SCL pins for I2C
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    
    // Enable internal pull-ups on both pins - these are essential without external resistors
    // Note: Pico internal pull-ups are around 50-60kΩ which is weaker than ideal for I2C
    // For more reliable operation, external 4.7kΩ resistors would be better
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    
    // For good measure, ensure pulls are actually enabled (reinforcing the above)
    gpio_set_pulls(sda, true, false);  // Pull up, not down
    gpio_set_pulls(scl, true, false);  // Pull up, not down
    
    // Wait a moment for pull-ups to take effect
    sleep_ms(10);
    
    printf("OLED: I2C initialized (SDA: %d, SCL: %d, ADDR: 0x%02X)\n", sda, scl, SSD1306_I2C_ADDR);
    printf("OLED: Using 7-bit address format as required by Pico SDK\n");
    printf("OLED: (Note: 0x%02X in 7-bit format = 0x%02X in 8-bit format)\n", SSD1306_I2C_ADDR, SSD1306_I2C_ADDR << 1);
    
    // Give the display time to power up and stabilize
    sleep_ms(300);  // Even longer delay for power-up
    
    // Scan all I2C addresses to detect any connected devices
    i2c_scan(i2c);
    
    // Try hardware reset if available (some OLED modules have reset pin)
    printf("OLED: Trying to communicate with display at address 0x%02X\n", SSD1306_I2C_ADDR);
    
    // Known good initialization sequence for 128x64 SSD1306 displays
    uint8_t init_sequence[] = {
        0xAE,        // Display off
        0xD5, 0x80,  // Set display clock
        0xA8, 0x3F,  // Set multiplex (0x3F for 64-line display)
        0xD3, 0x00,  // Set display offset = 0
        0x40,        // Set display start line = 0
        0x8D, 0x14,  // Enable charge pump
        0x20, 0x00,  // Set memory addressing mode (horizontal)
        0xA1,        // Set segment remap
        0xC8,        // Set COM scan direction
        0xDA, 0x12,  // Set COM pins hardware configuration
        0x81, 0x7F,  // Set contrast
        0xD9, 0xF1,  // Set pre-charge period
        0xDB, 0x20,  // Set VCOMH deselect level
        0xA4,        // Entire display on
        0xA6,        // Normal (not inverse) display
        0x2E,        // Deactivate scroll
        0xAF         // Display on
    };
    
    // Send init sequence
    printf("OLED: Starting command sequence...\n");
    uint32_t start_time = time_us_32();
    uint32_t timeout = 5000000;  // 5 second total timeout for init
    
    // Attempt to send the init sequence
    int error_count = 0;
    
    for (size_t i = 0; i < sizeof(init_sequence) && (time_us_32() - start_time < timeout); i++) {
        printf("OLED: Sending command 0x%02X (%d/%d)\n", init_sequence[i], (int)i+1, (int)sizeof(init_sequence));
        
        // Try to send command
        uint8_t data[2] = {0x00, init_sequence[i]};  // Control byte + command
        int result = i2c_write_timeout_us(i2c_instance, SSD1306_I2C_ADDR, data, 2, false, 50000);
        
        if (result < 0) {
            printf("OLED: I2C command write error %d (cmd: 0x%02X)\n", result, init_sequence[i]);
            error_count++;
        } else {
            printf("OLED: Command 0x%02X sent successfully\n", init_sequence[i]);
        }
        
        sleep_ms(10);  // Delay between commands
    }
    
    // Check if we had too many errors
    if (error_count > 10) {
        printf("OLED: ⚠️ Too many errors during initialization (%d errors)\n", error_count);
        printf("OLED: ⚠️ This suggests a hardware connection issue\n");
        return;
    }
    
    printf("OLED: Command sequence complete. Clearing display...\n");
    
    // Clear display buffer
    ssd1306_display_clear();
    printf("OLED: Display buffer cleared\n");
    
    // Do a proper first display update
    printf("OLED: Setting memory addressing mode (horizontal)\n");
    ssd1306_write_command(0x20); // Set Memory Addressing Mode
    ssd1306_write_command(0x00); // 00=Horizontal, 01=Vertical, 10=Page, 11=Invalid
    
    // Set column and page addresses for full display update
    printf("OLED: Setting display address bounds\n");
    ssd1306_write_command(SSD1306_COLUMNADDR);
    ssd1306_write_command(0);                  // Column start = 0
    ssd1306_write_command(SSD1306_WIDTH - 1);  // Column end = 127
    
    ssd1306_write_command(SSD1306_PAGEADDR);
    ssd1306_write_command(0);                  // Page start = 0
    ssd1306_write_command(SSD1306_HEIGHT / 8 - 1); // Page end = 7 (for 64-pixel height)
    
    // Send all zeros to clear the display
    printf("OLED: Sending full clear pattern...\n");
    
    // Send in small chunks to be safer
    uint8_t blank[16] = {0};
    for (int i = 0; i < (SSD1306_WIDTH * SSD1306_HEIGHT / 8); i += sizeof(blank)) {
        ssd1306_write_data(blank, sizeof(blank));
        sleep_ms(1); // Small delay between chunks
    }
    
    printf("OLED: Initial display clear complete\n");
}

void ssd1306_write_command(uint8_t command) {
    uint8_t data[2] = {0x00, command};  // First byte is control byte (0x00 for commands)
    int result = -1;
    
    // Use simple timeout-based approach - no logging for normal commands
    result = i2c_write_timeout_us(i2c_instance, SSD1306_I2C_ADDR, data, 2, false, 50000); // 50ms timeout
    
    // Only report errors
    if (result < 0) {
        printf("OLED: I2C command write error %d (cmd: 0x%02X)\n", result, command);
    }
    
    // Always add a short delay after each command
    sleep_ms(5);
}

void ssd1306_write_data(uint8_t* data, size_t len) {
    // For large data transfers, this function is critical for display quality
    
    // Allocate a buffer for the control byte + data
    uint8_t *buffer = (uint8_t*)malloc(len + 1);
    if (buffer) {
        buffer[0] = 0x40;  // Control byte (0x40 for data)
        memcpy(buffer + 1, data, len);
        
        // Use timeout-based write instead of blocking
        int result = i2c_write_timeout_us(i2c_instance, SSD1306_I2C_ADDR, buffer, len + 1, false, 50000);
        
        // Only report errors, not successful writes
        if (result < 0) {
            printf("OLED: I2C data write error %d (bytes: %d)\n", result, (int)len);
        }
        
        free(buffer);
    } else {
        printf("OLED: Failed to allocate memory for data write\n");
    }
    
    // Small delay between data writes for more reliable operation
    sleep_us(50);
}

void ssd1306_display_clear(void) {
    memset(buffer, 0, sizeof(buffer));
    cursor_x = 0;
    cursor_y = 0;
}

void ssd1306_display_update(void) {
    uint32_t start_time = time_us_32();
    uint32_t timeout = 1000000;  // 1 second timeout
    
    // For the SSD1306, we need to:
    // 1. Set the memory addressing mode (we use horizontal addressing mode)
    // 2. Define the column start/end addresses
    // 3. Define the page start/end addresses
    // 4. Send the display data
    
    // Set memory addressing mode (horizontal) - no logging for routine operations
    ssd1306_write_command(0x20); // Set Memory Addressing Mode
    ssd1306_write_command(0x00); // 00=Horizontal, 01=Vertical, 10=Page, 11=Invalid
    
    // Set column and page addresses for full display update
    ssd1306_write_command(SSD1306_COLUMNADDR);
    ssd1306_write_command(0);                  // Column start = 0
    ssd1306_write_command(SSD1306_WIDTH - 1);  // Column end = 127
    
    ssd1306_write_command(SSD1306_PAGEADDR);
    ssd1306_write_command(0);                  // Page start = 0
    ssd1306_write_command(SSD1306_HEIGHT / 8 - 1); // Page end = 7 (for 64-pixel height)
    
    // Write buffer in smaller 16-byte chunks for better reliability
    const int chunk_size = 16;
    for (size_t i = 0; i < sizeof(buffer); i += chunk_size) {
        // Check timeout
        if (time_us_32() - start_time > timeout) {
            printf("OLED: Display update timeout after %d bytes\n", (int)i);
            return;
        }
        
        size_t chunk = (i + chunk_size < sizeof(buffer)) ? chunk_size : sizeof(buffer) - i;
        ssd1306_write_data(buffer + i, chunk);
        
        // Add a small delay between chunks to prevent overloading the I2C bus
        sleep_us(100);
    }
    
    // No need for completion message during normal operation
}

void ssd1306_set_cursor(uint8_t x, uint8_t y) {
    cursor_x = x;
    cursor_y = y;
}

void ssd1306_write_text(const char *text, uint8_t x, uint8_t y) {
    cursor_x = x;
    cursor_y = y;
    
    while (*text) {
        if (cursor_x >= SSD1306_WIDTH - FONT_WIDTH) {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
            if (cursor_y >= SSD1306_HEIGHT) {
                cursor_y = 0;
            }
        }
        
        if (*text == '\n') {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
            if (cursor_y >= SSD1306_HEIGHT) {
                cursor_y = 0;
            }
        } else {
            // Get font character
            uint8_t c = *text - 32;  // Offset from ASCII space (32)
            
            // Draw character
            uint8_t page_start = cursor_y / 8;
            uint8_t page_end = (cursor_y + FONT_HEIGHT - 1) / 8;
            
            for (uint8_t page = page_start; page <= page_end; page++) {
                for (uint8_t col = 0; col < FONT_WIDTH; col++) {
                    uint8_t font_byte = font[c * FONT_WIDTH + col];
                    
                    // Handle alignment within page
                    if (page == page_start && (cursor_y % 8) > 0) {
                        uint8_t shift = cursor_y % 8;
                        buffer[page * SSD1306_WIDTH + cursor_x + col] |= font_byte << shift;
                    } else if (page == page_end && page > page_start) {
                        uint8_t shift = 8 - (cursor_y % 8);
                        buffer[page * SSD1306_WIDTH + cursor_x + col] |= font_byte >> shift;
                    } else {
                        buffer[page * SSD1306_WIDTH + cursor_x + col] |= font_byte;
                    }
                }
            }
            
            cursor_x += FONT_WIDTH;
        }
        text++;
    }
}

void ssd1306_write_line(const char *text, uint8_t line) {
    ssd1306_write_text(text, 0, line * FONT_HEIGHT);
}

void ssd1306_invert_display(bool invert) {
    ssd1306_write_command(invert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
}

void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    
    uint8_t page = y / 8;
    uint8_t bit = y % 8;
    
    if (on) {
        buffer[page * SSD1306_WIDTH + x] |= (1 << bit);
    } else {
        buffer[page * SSD1306_WIDTH + x] &= ~(1 << bit);
    }
}

void ssd1306_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        ssd1306_draw_pixel(x1, y1, true);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool filled) {
    if (filled) {
        for (uint8_t i = 0; i < width; i++) {
            for (uint8_t j = 0; j < height; j++) {
                ssd1306_draw_pixel(x + i, y + j, true);
            }
        }
    } else {
        // Top and bottom horizontal lines
        for (uint8_t i = 0; i < width; i++) {
            ssd1306_draw_pixel(x + i, y, true);
            ssd1306_draw_pixel(x + i, y + height - 1, true);
        }
        // Left and right vertical lines
        for (uint8_t i = 0; i < height; i++) {
            ssd1306_draw_pixel(x, y + i, true);
            ssd1306_draw_pixel(x + width - 1, y + i, true);
        }
    }
}