#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

// Display dimensions
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

// For Pico SDK, we need to use the address as a 7-bit value
// If your device is at 0x78 (8-bit format), we use 0x3C (7-bit format)
#define SSD1306_I2C_ADDR 0x3C

// SSD1306 commands
#define SSD1306_SETCONTRAST 0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_DISPLAYALLON 0xA5
#define SSD1306_NORMALDISPLAY 0xA6
#define SSD1306_INVERTDISPLAY 0xA7
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_SETLOWCOLUMN 0x00
#define SSD1306_SETHIGHCOLUMN 0x10
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22
#define SSD1306_COMSCANINC 0xC0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_CHARGEPUMP 0x8D
#define SSD1306_EXTERNALVCC 0x1
#define SSD1306_SWITCHCAPVCC 0x2

// Scrolling commands
#define SSD1306_ACTIVATE_SCROLL 0x2F
#define SSD1306_DEACTIVATE_SCROLL 0x2E
#define SSD1306_SET_VERTICAL_SCROLL_AREA 0xA3
#define SSD1306_RIGHT_HORIZONTAL_SCROLL 0x26
#define SSD1306_LEFT_HORIZONTAL_SCROLL 0x27
#define SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29
#define SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL 0x2A

// Font related
#define FONT_WIDTH 6
#define FONT_HEIGHT 8

// Function prototypes
bool ssd1306_detect(i2c_inst_t *i2c, uint8_t addr);
void i2c_scan(i2c_inst_t *i2c);
void ssd1306_init(i2c_inst_t *i2c, uint8_t sda, uint8_t scl);
void ssd1306_display_clear(void);
void ssd1306_display_update(void);
void ssd1306_write_command(uint8_t command);
void ssd1306_write_data(uint8_t* data, size_t len);
void ssd1306_set_cursor(uint8_t x, uint8_t y);
void ssd1306_write_text(const char *text, uint8_t x, uint8_t y);
void ssd1306_write_line(const char *text, uint8_t line);
void ssd1306_invert_display(bool invert);
void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on);
void ssd1306_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool filled);

#endif // SSD1306_H