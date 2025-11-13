/* Z1 Computer Tests */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

// FatFS includes
#include "ff.h"
#include "diskio.h"
#include "hw_config.h"

// PSRAM RP2350 Library
#include "psram_rp2350.h"

// Z1 Matrix Bus Library
#include "z1_matrix_bus.h"

// OLED Display Library
#include "ssd1306.h"

// PSRAM Pin Definitions  
#define PSRAM_CS_PIN     47    // GPIO47 for PSRAM CS

// SD Card Pin Definitions (SPI1)
#define SD_SPI_PORT      spi1
#define SD_MISO_PIN      40
#define SD_CLK_PIN       42 
#define SD_MOSI_PIN      43
#define SD_CS_PIN        41

// Z1 Computer Bus GPIO Definitions
// Multi-master 16-bit bus connecting 17 RP2350B nodes (16 nodes + 1 controller)
// Theory: BUSACT pulled high, pull low to claim bus. Master drives CLK for sync transfers.
// BUSRD & BUSWR used to control data direction.
// BUSSELECT encodes target node address (0-15=nodes, 16=controller, 31=broadcast)
// All nodes keep pins high-impedance (except address lines) until ready to talk/receive.
// All nodes monitor address lines and activate when their address is selected.
//
// Protocol Design:
// 1. Master claims BUSACT, sets target address
// 2. Target pulls BUSACK low when ready (acknowledge)
// 3. Master drives CLK for synchronous data transfer on 16-bit bus
// 4. All transfers are writes: [0xAA] [Originating_Node_ID] [Command] [Data]
// 5. Responses use same format: [0xAA] [Responding_Node_ID] [Response] [Data]
// 6. Both nodes release pins to high-Z when complete
// 7. Exponential backoff if BUSACT busy during claim attempt
// 8. PIO monitors BUSSELECT lines for address matching (non-CPU blocking)
// Note: All bus pin definitions now come from z1_matrix_bus.h

// W5500 Pin Definitions
#define W5500_SPI_PORT spi0
#define W5500_SCK      38
#define W5500_MOSI     39
#define W5500_MISO     36
#define W5500_CS       37
#define W5500_RST      34
#define W5500_INT      35

// I2C0 OLED Display Pins
#define OLED_I2C       i2c0
#define OLED_SDA_PIN   28
#define OLED_SCL_PIN   29

// I2C1 OLED Display Pins
#define OLED_I2C1       i2c1
#define OLED_SDA1_PIN   30
#define OLED_SCL1_PIN   31

// LED Pin Definitions
#define LED_GREEN      44
#define LED_BLUE       45  
#define LED_RED        46

// Node Reset Pin
#define NODE_RESET_PIN 33

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
#define PHYCFGR_OPMD     0x40    // Operation mode (1=configure with OPMDC, 0=PHY)
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
#define S0_DHAR0         0x0006  // Socket Destination Hardware Address Register
#define S0_DIPR0         0x000C  // Socket Destination IP Address Register
#define S0_DPORT0        0x0010  // Socket Destination Port Register

// Block Select Bits (BSB)
#define COMMON_REG_BSB   0x00    // Common register block
#define SOCKET0_REG_BSB  0x08    // Socket 0 register block

// Function prototypes
uint8_t w5500_read_reg(uint16_t addr, uint8_t bsb);
void w5500_write_reg(uint16_t addr, uint8_t bsb, uint8_t data);
void check_network_status(void);
const char* get_socket_status_name(uint8_t status);

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
const uint8_t MAC_ADDRESS[] = {0x02, 0x08, 0xDC, 0x00, 0x00, 0x01};
const uint8_t IP_ADDRESS[]  = {192, 168, 1, 222};
const uint8_t GATEWAY[]     = {192, 168, 1, 254};
const uint8_t SUBNET_MASK[] = {255, 255, 255, 0};

// LED State Variables
volatile bool led_green_state = false;
volatile bool led_blue_state = false;
volatile bool led_red_state = false;

// Last Test Results
static char last_test_status[128] = "none";
static uint32_t last_test_time_ms = 0;
static float last_test_speed_mbps = 0.0f;

// SD Card File Structure
typedef struct {
    char name[256];
    uint32_t size;
    bool is_directory;
} sd_file_entry_t;

// SD Card Directory Data
#define MAX_SD_FILES 50
static sd_file_entry_t sd_files[MAX_SD_FILES];
static int sd_file_count = 0;

// SD Card Global State
static bool sd_card_initialized = false;
static char sd_status[64] = "Not initialized";

// OLED Display State
static bool oled_initialized = false;
static char oled_status[64] = "Not initialized";
static bool http_connection_active = false;

// Z1 Bus Node Discovery State
static bool z1_active_nodes[16] = {false};  // Bitmap of discovered active nodes
static uint8_t z1_active_node_count = 0;    // Count of active nodes
static char z1_node_discovery_status[64] = "Not scanned";

// OLED Display Status Function
void update_oled_status(const char* status) {
    // Only log important status changes
    printf("Status: %s\n", status);
    
    if (!oled_initialized) {
        return;  // Skip silently if not initialized
    }
    
    strncpy(oled_status, status, sizeof(oled_status) - 1);
    oled_status[sizeof(oled_status) - 1] = '\0';
    
    // Clear display buffer silently
    ssd1306_display_clear();
    
    // Header with Z1 Computer
    ssd1306_write_line("Z1 Computer", 0);
    
    // Draw a line separator
    ssd1306_draw_line(0, 10, 127, 10);
    
    // Show IP Address
    char ip_str[20];
    sprintf(ip_str, "IP: %d.%d.%d.%d", IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);
    ssd1306_write_line(ip_str, 2);
    
    // Show connection status
    const char* conn_status = http_connection_active ? "Connected" : "Listening";
    ssd1306_write_line(conn_status, 3);
    
    // Get and show network status
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    char link_status[22];
    sprintf(link_status, "Link: %s %s %s",
            (phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN",
            (phycfgr & PHYCFGR_SPD) ? "100M" : "10M",
            (phycfgr & PHYCFGR_DPX) ? "Full" : "Half");
    ssd1306_write_line(link_status, 4);
    
    // Show node status with discovered count
    char node_status[22];
    sprintf(node_status, "Nodes: %d/16", z1_active_node_count);
    ssd1306_write_line(node_status, 5);
    
    // Show current status message
    if (strlen(status) > 21) {
        // Split long message into two lines
        char status_line1[22];
        char status_line2[22];
        strncpy(status_line1, status, 21);
        status_line1[21] = '\0';
        strncpy(status_line2, status + 21, 21);
        status_line2[21] = '\0';
        
        ssd1306_write_line(status_line1, 6);
        ssd1306_write_line(status_line2, 7);
    } else {
        // Show short message on one line
        ssd1306_write_line(status, 6);
    }
    
    // Update display
    ssd1306_display_update();
}

// FatFs file system variables
static FATFS fs;        // File system object
static bool fs_mounted = false;

// SD Card Performance Test Results
static char sd_test_results[256] = "No tests performed yet";

// Forward declarations
static bool load_sd_directory(void);

// Function declarations for LED effects
void clear_all_leds();
void turn_on_node(uint8_t node, uint8_t command);
void turn_off_node(uint8_t node, uint8_t command);
void run_chase_pattern();
void run_random_effect();
void run_rainbow_effect();
void reset_nodes(void);
bool send_chunked_http_response(const char* html_body, uint16_t body_length);
void generate_bus_test_page(char* buffer, size_t buffer_size);

// ============================================================================
// Z1 Computer Matrix Bus Control System
// ============================================================================

// Z1 Computer Matrix Bus Configuration
#define Z1_CONTROLLER_ID     16        // This node's ID



// Set address on the bus select lines  
static void z1_bus_set_address(uint8_t address) {
    gpio_put(BUSSELECT0_PIN, (address >> 0) & 1);
    gpio_put(BUSSELECT1_PIN, (address >> 1) & 1);
    gpio_put(BUSSELECT2_PIN, (address >> 2) & 1);
    gpio_put(BUSSELECT3_PIN, (address >> 3) & 1);
    gpio_put(BUSSELECT4_PIN, (address >> 4) & 1);
}

// Set data on the 16-bit data bus
static void z1_bus_set_data(uint16_t data) {
    // Switch data bus to output
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_OUT);
        gpio_put(BUS0_PIN + i, (data >> i) & 1);
    }
}

// Read data from the 16-bit data bus
static uint16_t z1_bus_get_data(void) {
    uint16_t data = 0;
    for (int i = 0; i < 16; i++) {
        if (gpio_get(BUS0_PIN + i)) {
            data |= (1 << i);
        }
    }
    return data;
}

// Wait for bus acknowledge with timeout
static bool z1_bus_wait_for_ack(uint32_t timeout_ms) {
    printf("[Z1 Bus] Waiting for ACK (BUSACK to go low), timeout=%dms\n", timeout_ms);
    printf("[Z1 Bus] Initial BUSACK state: %d\n", gpio_get(BUSACK_PIN));
    
    absolute_time_t timeout_time = make_timeout_time_ms(timeout_ms);
    uint32_t poll_count = 0;
    while (!time_reached(timeout_time)) {
        uint8_t current_busack = gpio_get(BUSACK_PIN);
        if (!current_busack) {  // ACK is active low
            printf("[Z1 Bus] ✅ ACK received after %d polls (BUSACK=0)\n", poll_count);
            return true;
        }
        poll_count++;
        if (poll_count % 1000 == 0) {  // Debug every 1000 polls
            printf("[Z1 Bus] Still waiting for ACK... polls=%d, BUSACK=%d\n", poll_count, current_busack);
        }
        sleep_us(10);
    }
    printf("[Z1 Bus] ❌ ACK TIMEOUT after %d polls, BUSACK still=%d\n", poll_count, gpio_get(BUSACK_PIN));
    return false;
}

// Release bus pins to high-impedance
static void z1_bus_release(void) {
    // Release control pins
    gpio_put(BUSACT_PIN, 1);  // Release bus activity
    gpio_put(BUSCLK_PIN, 0);
    
    // Set data bus back to input (high-Z)
    for (int i = 0; i < 16; i++) {
        gpio_set_dir(BUS0_PIN + i, GPIO_IN);
        gpio_disable_pulls(BUS0_PIN + i);
    }
}




// After testing more than a dozen implementations, this one was the closest for PSRAM
// Memory Usage Functions
static uint32_t get_free_ram_kb(void) {
    // Get actual free memory by calculating available heap space
    extern char __StackLimit, __bss_end__;
    char stack_var;
    
    // Current stack pointer position
    uint32_t current_stack = (uint32_t)&stack_var;
    
    // End of BSS (where heap starts)
    uint32_t heap_start = (uint32_t)&__bss_end__;
    
    // Available heap = current stack position - heap start - safety margin
    uint32_t stack_limit = (uint32_t)&__StackLimit;
    uint32_t available_heap = current_stack - heap_start - 2048; // 2KB safety margin
    
    // Make sure we don't return negative or invalid values
    if (current_stack <= heap_start + 2048) {
        return 1; // Almost out of memory
    }
    
    return available_heap / 1024;
}

static uint32_t get_used_ram_kb(void) {
    return 520 - get_free_ram_kb(); // Total - free = used
}

static uint32_t get_total_ram_kb(void) {
    // RP2350 has 520KB total SRAM
    return 520;
}

static uint32_t get_flash_usage_kb(void) {
    // Get actual flash usage from linker symbols - this is DYNAMIC
    extern char __flash_binary_start, __flash_binary_end;
    uint32_t flash_used = (uint32_t)(&__flash_binary_end - &__flash_binary_start);
    return flash_used / 1024;
}

static uint32_t get_total_flash_mb(void) {
    // Get actual flash size dynamically from flash chip
    // The Pico SDK can detect flash size at runtime
    extern char __flash_binary_end;
    uint32_t flash_end = (uint32_t)&__flash_binary_end;
    
    // For RP2350, we can try to detect flash size by reading flash registers
    // For now, we'll use a more conservative detection based on available space
    // Most Pico2 boards have 16MB, but some might have 4MB or 8MB
    if (flash_end < 4 * 1024 * 1024) {
        return 4;  // 4MB flash
    } else if (flash_end < 8 * 1024 * 1024) {
        return 8;  // 8MB flash  
    } else {
        return 16; // 16MB flash (most common)
    }
}

static uint32_t get_free_flash_mb(void) {
    // Calculate free flash space dynamically
    uint32_t total_flash_kb = get_total_flash_mb() * 1024;
    uint32_t used_flash_kb = get_flash_usage_kb();
    uint32_t free_flash_kb = total_flash_kb - used_flash_kb;
    return free_flash_kb / 1024; // Return in MB
}


// PSRAM Memory Test using the library
uint32_t test_psram_memory(uint32_t test_bytes) {
    if (!psram_is_initialized()) {
        printf("[ERROR] PSRAM not initialized\n");
        return 0;
    }
    
    printf("[TEST] Testing %.1f MB PSRAM...\n", (float)test_bytes / (1024.0f * 1024.0f));
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t errors = 0;
    
    const uint32_t chunk_size = 8 * 1024;  // 8KB chunks
    volatile uint8_t *base_ptr = psram_get_pointer(0);
    
    if (!base_ptr) {
        printf("[ERROR] Failed to get PSRAM pointer\n");
        return 1;
    }
    
    for (uint32_t offset = 0; offset < test_bytes; offset += chunk_size) {
        uint32_t current_chunk = (offset + chunk_size > test_bytes) ? (test_bytes - offset) : chunk_size;
        volatile uint8_t *chunk_ptr = base_ptr + offset;
        
        memset((void*)chunk_ptr, 0xA5, current_chunk);
        
        if (chunk_ptr[0] != 0xA5 || chunk_ptr[current_chunk-1] != 0xA5) {
            errors++;
            if (errors > 20) break;
        }
        
        if ((offset & ((1024 * 1024) - 1)) == 0 && offset > 0) {
            printf("  %.1f MB\n", (float)offset / (1024.0f * 1024.0f));
        }
    }
    
    uint32_t test_time = to_ms_since_boot(get_absolute_time()) - start_time;
    float throughput_mbps = ((float)test_bytes / (1024.0f * 1024.0f)) / ((float)test_time / 1000.0f);
    
    last_test_time_ms = test_time;
    last_test_speed_mbps = throughput_mbps;
    
    if (errors == 0) {
        printf("✅ Test passed: %.1f MB in %lu ms (%.2f MB/s)\n", 
               (float)test_bytes / (1024.0f * 1024.0f), 
               (unsigned long)test_time, throughput_mbps);
        snprintf(last_test_status, sizeof(last_test_status), 
                "success - %.1f MB in %lu ms (%.2f MB/s)", 
                (float)test_bytes / (1024.0f * 1024.0f), 
                (unsigned long)test_time, throughput_mbps);
    } else {
        printf("❌ Test failed: %lu errors\n", (unsigned long)errors);
        snprintf(last_test_status, sizeof(last_test_status), 
                "failed - %lu errors", (unsigned long)errors);
    }
    
    uint32_t used_ram = get_used_ram_kb();
    uint32_t free_ram = get_free_ram_kb();
    
    printf("System: %lu KB RAM used, %lu KB free\n", 
           (unsigned long)used_ram, (unsigned long)free_ram);
    
    return errors;
}

// SD Card Functions
static bool sd_test_basic_write(void) {
    if (!sd_card_initialized || !fs_mounted) {
        printf("SD card not initialized for basic write test\n");
        return false;
    }
    
    const char* test_file = "0:/test.txt";
    FIL file;
    FRESULT result = f_open(&file, test_file, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        printf("Basic write test failed - f_open error: %d\n", result);
        return false;
    }
    
    const char* test_data = "Hello SD Card!\n";
    UINT bytes_written;
    result = f_write(&file, test_data, strlen(test_data), &bytes_written);
    f_close(&file);
    
    if (result == FR_OK && bytes_written == strlen(test_data)) {
        printf("Basic write test passed - %d bytes written\n", bytes_written);
        f_unlink(test_file);
        return true;
    } else {
        printf("Basic write test failed - f_write error: %d, bytes: %d\n", result, bytes_written);
        return false;
    }
}

static bool sd_card_init(void) {
    if (sd_card_initialized) {
        return true;
    }
    
    printf("Initializing SD card...\n");
    
    // Initialize the SD driver
    sd_init_driver();
    
    // Mount the file system
    FRESULT result = f_mount(&fs, "0:", 1);
    if (result == FR_OK) {
        sd_card_initialized = true;
        fs_mounted = true;
        printf("SD card with FAT32 file system mounted successfully\n");
        
        // Test basic write functionality
        if (sd_test_basic_write()) {
            snprintf(sd_status, sizeof(sd_status), "FAT32 file system mounted and write test passed");
        } else {
            snprintf(sd_status, sizeof(sd_status), "FAT32 mounted but write test failed");
        }
        
        // Load directory contents
        load_sd_directory();
        
        return true;
    } else {
        snprintf(sd_status, sizeof(sd_status), "FAT32 mount failed (error: %d)", result);
        printf("SD card FAT32 mount failed: error %d\n", result);
        return false;
    }
}



// SD Card Test Functions
static bool sd_performance_test(const char* filename, uint32_t size_mb) {
    if (!sd_card_initialized || !fs_mounted) {
        snprintf(sd_test_results, sizeof(sd_test_results), "ERROR: SD card not initialized");
        return false;
    }
    
    // Calculate actual size in bytes
    uint32_t total_bytes;
    if (size_mb == 0) {
        // Special case: 0 means 128K
        total_bytes = 128 * 1024;  // 128KB
        printf("Starting SD card performance test: %s (128K)\n", filename);
    } else {
        total_bytes = size_mb * 1024 * 1024;
        printf("Starting SD card performance test: %s (%lu MB)\n", filename, size_mb);
    }
    
    // Build full path with volume prefix
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "0:/%s", filename);
    
    // Delete file if it exists
    f_unlink(full_path);
    uint32_t chunk_size = 4096;
    
    static uint8_t test_buffer[4096];
    for (uint32_t i = 0; i < chunk_size; i++) {
        test_buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    // Start timing
    uint32_t start_time = time_us_32();
    
    FIL file;
    FRESULT result = f_open(&file, full_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        snprintf(sd_test_results, sizeof(sd_test_results), "ERROR: Failed to create %s (FatFS error: %d)", full_path, result);
        printf("f_open failed: %s, error code: %d\n", full_path, result);
        return false;
    }
    
    uint32_t bytes_written_total = 0;
    uint32_t chunks_to_write = total_bytes / chunk_size;
    
    for (uint32_t chunk = 0; chunk < chunks_to_write; chunk++) {
        UINT bytes_written;
        result = f_write(&file, test_buffer, chunk_size, &bytes_written);
        
        if (result != FR_OK || bytes_written != chunk_size) {
            f_close(&file);
            snprintf(sd_test_results, sizeof(sd_test_results), "ERROR: Write failed at chunk %lu (error: %d)", chunk, result);
            return false;
        }
        
        bytes_written_total += bytes_written;
        
        if (size_mb >= 10 && chunk % 64 == 0 && chunk > 0) {
            printf("Written: %lu MB / %lu MB\n", bytes_written_total / (1024*1024), size_mb);
        }
    }
    
    // Sync to ensure data is written to SD card
    f_sync(&file);
    
    f_close(&file);
    
    uint32_t end_time = time_us_32();
    uint32_t elapsed_us = end_time - start_time;
    float elapsed_seconds = (float)elapsed_us / 1000000.0f;
    float speed_mbps = (float)bytes_written_total / (elapsed_seconds * 1024.0f * 1024.0f);
    
    if (size_mb == 0) {
        snprintf(sd_test_results, sizeof(sd_test_results), 
            "SUCCESS: 128K written in %.2f seconds (%.2f MB/s)", 
            elapsed_seconds, speed_mbps);
    } else {
        snprintf(sd_test_results, sizeof(sd_test_results), 
            "SUCCESS: %lu MB written in %.2f seconds (%.2f MB/s)", 
            size_mb, elapsed_seconds, speed_mbps);
    }
    
    printf("SD Performance Test Complete: %s\n", sd_test_results);
    
    f_unlink(full_path);
    
    return true;
}

static bool sd_delete_file(const char* filename) {
    if (!sd_card_initialized || !fs_mounted) {
        return false;
    }
    
    printf("Deleting file: %s\n", filename);
    
    FRESULT result = f_unlink(filename);
    if (result == FR_OK) {
        printf("File deleted successfully: %s\n", filename);
        return true;
    } else {
        printf("Failed to delete file %s (error: %d)\n", filename, result);
        return false;
    }
}

static bool load_sd_directory(void) {
    if (!sd_card_initialized || !fs_mounted) {
        return false;
    }
    
    DIR dir;
    FILINFO file_info;
    FRESULT result;
    
    // Clear existing data
    sd_file_count = 0;
    
    // Open root directory
    result = f_opendir(&dir, "/");
    if (result != FR_OK) {
        return false;
    }
    
    // Read directory entries into memory structure
    while (f_readdir(&dir, &file_info) == FR_OK && file_info.fname[0] != 0) {
        if (sd_file_count >= MAX_SD_FILES) break;
        
        // Copy file info to our structure
        strncpy(sd_files[sd_file_count].name, file_info.fname, sizeof(sd_files[sd_file_count].name) - 1);
        sd_files[sd_file_count].name[sizeof(sd_files[sd_file_count].name) - 1] = '\0';
        sd_files[sd_file_count].size = file_info.fsize;
        sd_files[sd_file_count].is_directory = (file_info.fattrib & AM_DIR) != 0;
        
        sd_file_count++;
    }
    
    f_closedir(&dir);
    return true;
}

// SPI Functions
void w5500_select(void) {
    gpio_put(W5500_CS, 0);
    sleep_us(1);
}

void w5500_deselect(void) {
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

// Helper function to send raw data to W5500 buffer
bool send_raw_data(const char* data, uint16_t length) {
    // Wait for sufficient TX buffer space
    int buffer_wait_count = 0;
    while (buffer_wait_count < 50) {
        uint8_t tx_fsr_high = w5500_read_reg(0x0020, SOCKET0_REG_BSB);
        uint8_t tx_fsr_low = w5500_read_reg(0x0021, SOCKET0_REG_BSB);
        uint16_t tx_free_space = (tx_fsr_high << 8) | tx_fsr_low;
        
        if (tx_free_space >= length) {
            break;
        }
        
        printf("WARNING: Insufficient TX buffer space (%d < %d), waiting... (%d/50)\n", 
               tx_free_space, length, buffer_wait_count + 1);
        sleep_ms(20);
        buffer_wait_count++;
    }
    
    if (buffer_wait_count >= 50) {
        printf("ERROR: Timeout waiting for TX buffer space\n");
        return false;
    }
    
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
    
    // Send data
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_SEND);
    
    // Wait for send command to complete
    int send_timeout = 100;
    uint8_t cmd_status;
    do {
        cmd_status = w5500_read_reg(S0_CR, SOCKET0_REG_BSB);
        sleep_ms(5);
        send_timeout--;
    } while (cmd_status != 0 && send_timeout > 0);
    
    if (send_timeout == 0) {
        printf("ERROR: Timeout sending data\n");
        return false;
    }
    
    return true;
}

// Function to send proper HTTP/1.1 chunked response
bool send_chunked_http_response(const char* html_body, uint16_t body_length) {
    const uint16_t DATA_CHUNK_SIZE = 1000;  // Size of HTML data per chunk
    
    printf("Sending HTTP/1.1 chunked response: %d bytes HTML body\n", body_length);
    
    // First, send HTTP headers with Transfer-Encoding: chunked
    // Add timestamp-based ETag to prevent any caching
    static uint32_t request_counter = 0;
    request_counter++;
    
    char headers[512];
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate, max-age=0, private\r\n"
        "Pragma: no-cache\r\n"
        "Expires: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
        "Last-Modified: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
        "ETag: \"%u-%u\"\r\n"
        "Vary: *\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n", request_counter, (uint32_t)time_us_32());
    
    printf("Sending HTTP headers with chunked encoding and strong anti-caching (ETag: %u-%u)...\n", request_counter, (uint32_t)time_us_32());
    if (!send_raw_data(headers, strlen(headers))) {
        printf("ERROR: Failed to send HTTP headers\n");
        return false;
    }
    
    // Wait for headers to be transmitted
    sleep_ms(50);
    
    // Now send body data in properly formatted HTTP chunks
    uint16_t bytes_sent = 0;
    uint16_t chunk_number = 1;
    
    while (bytes_sent < body_length) {
        // Calculate chunk size for this iteration
        uint16_t remaining = body_length - bytes_sent;
        uint16_t chunk_data_size = (remaining > DATA_CHUNK_SIZE) ? DATA_CHUNK_SIZE : remaining;
        
        printf("Sending HTTP chunk %d: %d bytes (offset %d)\n", chunk_number, chunk_data_size, bytes_sent);
        
        // Format chunk size in hex + CRLF
        char chunk_header[16];
        snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", chunk_data_size);
        
        printf("Chunk header: '%s' (hex size: %X)\n", chunk_header, chunk_data_size);
        
        // Send chunk size header
        if (!send_raw_data(chunk_header, strlen(chunk_header))) {
            printf("ERROR: Failed to send chunk header %d\n", chunk_number);
            return false;
        }
        
        // Send chunk data
        if (!send_raw_data(&html_body[bytes_sent], chunk_data_size)) {
            printf("ERROR: Failed to send chunk data %d\n", chunk_number);
            return false;
        }
        
        // Send chunk trailing CRLF
        if (!send_raw_data("\r\n", 2)) {
            printf("ERROR: Failed to send chunk trailer %d\n", chunk_number);
            return false;
        }
        
        // Wait for buffer to drain before next chunk
        int drain_timeout = 50;
        while (drain_timeout > 0) {
            uint8_t tx_fsr_high = w5500_read_reg(0x0020, SOCKET0_REG_BSB);
            uint8_t tx_fsr_low = w5500_read_reg(0x0021, SOCKET0_REG_BSB);
            uint16_t tx_free_space = (tx_fsr_high << 8) | tx_fsr_low;
            
            if (tx_free_space > 1200) {
                printf("Buffer drained: %d bytes free\n", tx_free_space);
                break;
            }
            
            sleep_ms(5);
            drain_timeout--;
        }
        
        // Check if socket is still connected
        uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        if (status != SOCK_STAT_ESTABLISHED) {
            printf("ERROR: Socket disconnected during chunk %d (status: 0x%02X)\n", chunk_number, status);
            return false;
        }
        
        bytes_sent += chunk_data_size;
        chunk_number++;
        
        // Small delay between chunks
        if (bytes_sent < body_length) {
            sleep_ms(10);
        }
    }
    
    // Send final terminating chunk: "0\r\n\r\n"
    printf("Sending terminating chunk...\n");
    if (!send_raw_data("0\r\n\r\n", 5)) {
        printf("ERROR: Failed to send terminating chunk\n");
        return false;
    }
    
    printf("HTTP chunked transmission complete: %d bytes sent in %d chunks\n", bytes_sent, chunk_number - 1);
    return true;
}

void reset_phy(void) {
    printf("Resetting PHY...\n");
    // Read current PHY configuration
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    
    // Set the RST bit (bit 7) to reset PHY
    phycfgr |= PHYCFGR_RST;
    w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
    
    // Wait for the reset to complete
    sleep_ms(50);
    
    // Clear the RST bit
    phycfgr &= ~PHYCFGR_RST;
    w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
    
    // Wait for PHY to stabilize
    sleep_ms(200);
    
    printf("PHY reset complete\n");
}

void hardware_reset(void) {
    printf("Hardware reset...\n");
    gpio_put(W5500_RST, 0);
    sleep_ms(10);
    gpio_put(W5500_RST, 1);
    sleep_ms(200);
}

// Reset all nodes function - placeholder, implementation moved after global variables

bool init_hardware(void) {
    // Initialize PSRAM first
    printf("\n[PSRAM] Initializing PSRAM...\n");
    if (!psram_init()) {
        printf("⚠️  PSRAM initialization failed, continuing without PSRAM\n");
    }
    
    spi_init(W5500_SPI_PORT, 40000000);  // 40MHz SPI frequency
    gpio_set_function(W5500_SCK, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(W5500_MISO, GPIO_FUNC_SPI);
    
    gpio_init(W5500_CS);
    gpio_set_dir(W5500_CS, GPIO_OUT);
    gpio_put(W5500_CS, 1);
    
    gpio_init(W5500_RST);
    gpio_set_dir(W5500_RST, GPIO_OUT);
    gpio_put(W5500_RST, 1);
    
    // Initialize LEDs
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, led_green_state);
    
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, led_blue_state);
    
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, led_red_state);
    
    // Initialize Node Reset Pin (GPIO33)
    gpio_init(NODE_RESET_PIN);
    gpio_set_dir(NODE_RESET_PIN, GPIO_OUT);
    gpio_put(NODE_RESET_PIN, 0);  // Set low by default (nodes not in reset)
    
    hardware_reset();
    
    uint8_t version = w5500_read_reg(W5500_VERSIONR, COMMON_REG_BSB);
    printf("W5500 Version: 0x%02X\n", version);
    
    // Initialize SD card
    printf("\n[SD Card] Initializing SD card...\n");
    if (!sd_card_init()) {
        printf("⚠️  SD card initialization failed, continuing without SD card\n");
    }
    
    // Initialize Z1 Computer Matrix Bus
    printf("\n[Z1 Bus] Initializing Matrix Bus...\n");
    
    // Configure bus timing for fast parallel bus (tunable parameters)
    z1_bus_clock_high_us = 100;   // Clock high time between frames
    z1_bus_clock_low_us = 50;     // Clock low time for data latch
    z1_bus_ack_timeout_ms = 10;   // ACK wait timeout (fast bus - nodes should ACK immediately)
    z1_bus_backoff_base_us = 50;  // Base backoff for collisions (reduced from 100us)
    z1_bus_broadcast_hold_ms = 10; // Broadcast hold time (reduced from 50ms)
    
    printf("[Z1 Bus] Timing configured (fast parallel bus) - High:%dus, Low:%dus, ACK:%dms, Backoff:%dus, Broadcast:%dms\n",
           z1_bus_clock_high_us, z1_bus_clock_low_us, 
           z1_bus_ack_timeout_ms, z1_bus_backoff_base_us, z1_bus_broadcast_hold_ms);
    
    // Initialize Z1 Matrix Bus (Controller mode)
    z1_bus_init(Z1_CONTROLLER_ID);
    
    // Discover available nodes on the bus (sequential scan to avoid contention)
    printf("Starting node discovery (sequential scan)...\n");
    snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), "Scanning...");
    
    bool discovery_success = z1_discover_nodes_sequential(z1_active_nodes);
    
    if (discovery_success) {
        // Count active nodes
        z1_active_node_count = 0;
        for (int i = 0; i < 16; i++) {
            if (z1_active_nodes[i]) {
                z1_active_node_count++;
                printf("  Node %d: ACTIVE\n", i);
            }
        }
        printf("Discovery complete: %d/16 nodes found\n", z1_active_node_count);
        snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), 
                 "Found %d nodes", z1_active_node_count);
    } else {
        printf("Discovery failed\n");
        snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), "Scan failed");
    }
    
    return (version == 0x04);
}

void setup_network(void) {
    printf("Configuring network settings...\n");
    
    // Using the original implementation from the backup - no PHY configuration
    for (int i = 0; i < 6; i++) {
        w5500_write_reg(W5500_SHAR0 + i, COMMON_REG_BSB, MAC_ADDRESS[i]);
    }
    
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SIPR0 + i, COMMON_REG_BSB, IP_ADDRESS[i]);
        w5500_write_reg(W5500_GAR0 + i, COMMON_REG_BSB, GATEWAY[i]);
        w5500_write_reg(W5500_SUBR0 + i, COMMON_REG_BSB, SUBNET_MASK[i]);
    }
    
    // Read the PHY configuration for debugging purposes only
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("PHY Configuration: 0x%02X\n", phycfgr);
    printf("Link Status: %s\n", (phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN");
    printf("Speed: %s\n", (phycfgr & PHYCFGR_SPD) ? "100Mbps" : "10Mbps");
    printf("Duplex: %s\n", (phycfgr & PHYCFGR_DPX) ? "Full" : "Half");
    printf("Mode: %s (0x%X)\n", (phycfgr & PHYCFGR_OPMD) ? "Configured" : "Auto", 
           (phycfgr & PHYCFGR_OPMDC) >> 3);
    
    printf("Network configuration complete.\n");
}

// LED Control Functions
void update_led_state(uint8_t led_pin, bool state) {
    gpio_put(led_pin, state);
}

void set_led(int led_id, bool state) {
    switch (led_id) {
        case 0: // Green LED
            led_green_state = state;
            update_led_state(LED_GREEN, led_green_state);
            printf("Green LED set to %s (GPIO %d = %d)\n", led_green_state ? "ON" : "OFF", LED_GREEN, led_green_state ? 1 : 0);
            break;
        case 1: // Blue LED
            led_blue_state = state;
            update_led_state(LED_BLUE, led_blue_state);
            printf("Blue LED set to %s (GPIO %d = %d)\n", led_blue_state ? "ON" : "OFF", LED_BLUE, led_blue_state ? 1 : 0);
            break;
        case 2: // Red LED
            led_red_state = state;
            update_led_state(LED_RED, led_red_state);
            printf("Red LED set to %s (GPIO %d = %d)\n", led_red_state ? "ON" : "OFF", LED_RED, led_red_state ? 1 : 0);
            break;
    }
}

// Page type enumeration
typedef enum {
    PAGE_MAIN_MENU,
    PAGE_LED_TEST,
    PAGE_MEMORY_TEST,
    PAGE_SD_TEST,
    PAGE_BUS_TEST,
    PAGE_16K_TEST
} page_type_t;

// Current page state
static page_type_t current_page = PAGE_MAIN_MENU;

// Global bus activity monitoring
static char bus_activity_log[1024] = {0};
static int bus_activity_rows = 0;
static bool is_favicon_request = false;

// Z1 Bus Test State
static uint8_t z1_selected_node = 0;  // Currently selected target node
static char z1_bus_status[128] = "Ready";
static bool simple_ajax_response = false;  // Flag for AJAX requests that need simple responses

// Network status
static char network_status[128] = "Unknown";
static uint32_t last_network_check_ms = 0;

// Reset all nodes function - implementation
// Variables for link monitoring
static uint32_t link_down_start_ms = 0;
static uint8_t link_retry_count = 0;
static uint8_t current_phy_mode = 7; // Start with auto-negotiation (7)
static bool link_was_up = false;
static uint32_t link_up_time_ms = 0;
static uint32_t link_stability_check_ms = 0;

// Function to try different PHY modes
void try_next_phy_mode(void) {
    // Array of modes to try in sequence
    // 7 = Auto-negotiation (111)
    // 5 = 100BASE-TX Half Duplex (101) - Try Half Duplex first for Netgear compatibility
    // 1 = 100BASE-TX Full Duplex (001)
    // 4 = 10BASE-T Half Duplex (100)
    // 0 = 10BASE-T Full Duplex (000)
    // 2 = Reserved/Special (010) - Try as last resort
    static const uint8_t phy_modes[] = {7, 5, 1, 4, 0, 2};
    static const char* mode_names[] = {
        "10BASE-T Full Duplex", "100BASE-TX Full Duplex", "Special Mode", "Reserved",
        "10BASE-T Half Duplex", "100BASE-TX Half Duplex", "Reserved", "Auto-negotiation",
    };
    
    // Cycle through modes
    link_retry_count++;
    uint8_t mode_index = link_retry_count % 6;  // 6 modes to try
    current_phy_mode = phy_modes[mode_index];
    
    printf("Link down for too long, trying PHY mode %d (%s)...\n", 
           current_phy_mode, mode_names[current_phy_mode]);
    
    // Hard reset the W5500 every other attempt
    if (link_retry_count % 2 == 0) {
        printf("Performing full hardware reset...\n");
        hardware_reset();
        sleep_ms(300);  // Extra time for hardware to stabilize
    } else {
        // Reset PHY only
        reset_phy();
    }
    
    // Apply the new mode with explicit bit setting
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    
    // Clear all configuration bits first
    phycfgr &= ~(PHYCFGR_OPMDC | PHYCFGR_DPX | PHYCFGR_SPD);
    
    // Set operation mode config
    phycfgr |= PHYCFGR_OPMD | (current_phy_mode << 3);
    
    // Explicitly set duplex and speed bits based on mode for consistency
    if (current_phy_mode == 1 || current_phy_mode == 0) {
        // Full duplex modes
        phycfgr |= PHYCFGR_DPX;
    }
    
    if (current_phy_mode == 1 || current_phy_mode == 5) {
        // 100Mbps modes
        phycfgr |= PHYCFGR_SPD;
    }
    
    // Write configuration
    w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
    
    // Give it more time to establish link
    sleep_ms(1000);
    
    // Read back and print for verification
    phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("PHY configuration after setting mode %d: 0x%02X\n", current_phy_mode, phycfgr);
}

// Special hard reset function for complete reinitialization
void hard_reset_w5500(void) {
    printf("[NET] Performing complete W5500 hard reset and reinitialization...\n");
    
    // Hardware reset first
    hardware_reset();
    sleep_ms(300);  // Longer delay for hardware to stabilize
    
    // Reset PHY
    reset_phy();
    sleep_ms(300);  // Longer delay after PHY reset
    
    // Force special register writes (based on W5500 datasheet)
    w5500_write_reg(0x00, COMMON_REG_BSB, 0x80);  // Reset MR register
    sleep_ms(100);  // Wait longer after register reset
    
    // Read chip version to verify communication
    uint8_t version = w5500_read_reg(W5500_VERSIONR, COMMON_REG_BSB);
    printf("[NET] W5500 version after hard reset: 0x%02X\n", version);
    
    // Re-apply network configuration
    for (int i = 0; i < 6; i++) {
        w5500_write_reg(W5500_SHAR0 + i, COMMON_REG_BSB, MAC_ADDRESS[i]);
    }
    
    for (int i = 0; i < 4; i++) {
        w5500_write_reg(W5500_SIPR0 + i, COMMON_REG_BSB, IP_ADDRESS[i]);
        w5500_write_reg(W5500_GAR0 + i, COMMON_REG_BSB, GATEWAY[i]);
        w5500_write_reg(W5500_SUBR0 + i, COMMON_REG_BSB, SUBNET_MASK[i]);
    }
    
    printf("[NET] Network configuration reapplied\n");
}

// Special function to try to fix link issues with Netgear switch
void try_netgear_fix(void) {
    printf("[NET] Trying special fix for Netgear switch compatibility...\n");
    
    // First perform a hard reset
    hard_reset_w5500();
    
    // Based on observed PHY values in logs, we want to get to 0xEB value (Link UP)
    // Start with base settings similar to what we observed
    
    // 1. First try direct write of the known good value from logs
    printf("[NET] Trying direct write of working PHY value (0xEB)...\n");
    w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, 0xEB);  // Force the working value
    sleep_ms(1000);
    
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    if (phycfgr & PHYCFGR_LNK) {
        printf("[NET] Direct write successful! Link UP with PHY=0x%02X\n", phycfgr);
        return;
    }
    
    // 2. Set 100BASE-T Half Duplex mode specifically (mode 5)
    printf("[NET] Trying 100BASE-T Half Duplex (mode 5)...\n");
    phycfgr = 0xA8 | PHYCFGR_OPMD | (0x5 << 3) | PHYCFGR_SPD;
    w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
    sleep_ms(800);
    
    phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    if (phycfgr & PHYCFGR_LNK) {
        printf("[NET] 100BASE-T Half Duplex successful! Link UP with PHY=0x%02X\n", phycfgr);
        return;
    }
    
    // 3. Try disabling auto-negotiation on the switch side by forcing the speed
    printf("[NET] Trying special Netgear reset procedure...\n");
    
    // Write a sequence of PHY configs to try to get the switch to adjust
    for (int i = 0; i < 3; i++) {
        // First 10BASE-T
        phycfgr = 0xA8 | PHYCFGR_OPMD | (0x4 << 3);
        w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
        sleep_ms(300);
        
        // Then 100BASE-T Half Duplex
        phycfgr = 0xA8 | PHYCFGR_OPMD | (0x5 << 3) | PHYCFGR_SPD;
        w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
        sleep_ms(500);
        
        // Check if link came up
        phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
        if (phycfgr & PHYCFGR_LNK) {
            printf("[NET] Link came up during reset procedure! PHY=0x%02X\n", phycfgr);
            return;
        }
    }
    
    // Read final state
    phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("[NET] After special fix sequence: PHY=0x%02X, Link=%s\n", 
           phycfgr, (phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN");
}

// Function to stabilize the link if it's flickering
void stabilize_link(uint8_t current_phycfgr) {
    printf("[NET] Trying to stabilize flickering link (PHY: 0x%02X)...\n", current_phycfgr);
    
    // If we observed a brief link UP, try to enforce that exact configuration
    if (current_phycfgr & PHYCFGR_LNK) {
        // We have a link! Save this exact configuration and enforce it
        printf("[NET] Enforcing known good PHY configuration: 0x%02X\n", current_phycfgr);
        
        // Write the current good config multiple times to reinforce it
        for (int i = 0; i < 3; i++) {
            w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, current_phycfgr);
            sleep_ms(200);
        }
    } else {
        // Try to write 0xEB which worked briefly in the logs
        printf("[NET] Link flickering - trying to enforce known working value 0xEB\n");
        w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, 0xEB);
        sleep_ms(300);
        
        // Read back and check
        uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
        if (phycfgr & PHYCFGR_LNK) {
            printf("[NET] Link stabilized with 0xEB! PHY=0x%02X\n", phycfgr);
            
            // Write it again to reinforce
            w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, 0xEB);
        }
    }
    
    // Wait a moment for link to stabilize
    sleep_ms(500);
    
    // Final check
    uint8_t final_phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    printf("[NET] Link stabilization result: PHY=0x%02X, Link=%s\n", 
           final_phycfgr, (final_phycfgr & PHYCFGR_LNK) ? "UP" : "DOWN");
}

// Function to check and update network status information
void check_network_status(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Only check every 5 seconds to avoid constant polling
    if (now - last_network_check_ms < 5000) {
        return;
    }
    
    last_network_check_ms = now;
    
    // Read PHY configuration register
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    
    // Check link status
    bool link_up = (phycfgr & PHYCFGR_LNK) != 0;
    bool speed_100m = (phycfgr & PHYCFGR_SPD) != 0;
    bool full_duplex = (phycfgr & PHYCFGR_DPX) != 0;
    bool auto_negotiate_mode = (phycfgr & PHYCFGR_OPMD) != 0;
    uint8_t op_mode = (phycfgr & PHYCFGR_OPMDC) >> 3;
    
    // Format status message
    snprintf(network_status, sizeof(network_status), 
             "Network: Link %s | %s | %s | Mode: %s (0x%X)",
             link_up ? "UP" : "DOWN",
             speed_100m ? "100Mbps" : "10Mbps",
             full_duplex ? "Full Duplex" : "Half Duplex",
             auto_negotiate_mode ? "Auto" : "Force",
             op_mode);
    
    // No debug output to serial - status is stored in network_status variable if needed
    
    // Check for link flickering (was up but went down quickly)
    if (link_was_up && !link_up) {
        // No debug output for link flickering
        
        // Try to stabilize the link immediately if it was briefly up
        if (now - link_up_time_ms < 10000) { // If it was up within last 10 seconds
            stabilize_link(phycfgr);
            
            // Recheck after stabilization attempt
            phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
            link_up = (phycfgr & PHYCFGR_LNK) != 0;
        }
    }
    
    // Update link state tracking
    if (link_up) {
        if (!link_was_up) {
            // Link just came up, start tracking time
            link_up_time_ms = now;
            // No debug output for link up events
        }
        
        // Periodic link stability check - reinforce current config every 30 seconds
        if (now - link_stability_check_ms > 30000) {
            // No debug output for reinforcing link configuration
            w5500_write_reg(W5500_PHYCFGR, COMMON_REG_BSB, phycfgr);
            link_stability_check_ms = now;
        }
        
        link_was_up = true;
        link_down_start_ms = 0;  // Reset link down timer
    } else {
        // Link recovery logic
        if (link_down_start_ms == 0) {
            // Start counting link down time
            link_down_start_ms = now;
        } else if (now - link_down_start_ms > 15000) {  // Reduced to 15 seconds threshold
            // Every 4th attempt, try the Netgear specific fix
            if (link_retry_count % 4 == 3) {
                try_netgear_fix();
            } else {
                // Otherwise use standard mode cycling
                try_next_phy_mode();
            }
            
            // Reset timer
            link_down_start_ms = now;
        }
        link_was_up = false;
    }
}

void reset_nodes(void) {
    printf("Resetting all nodes via GPIO33...\n");
    
    // Set the pin high for reset
    gpio_put(NODE_RESET_PIN, 1);
    
    // Wait 100ms
    sleep_ms(100);
    
    // Set the pin back to low
    gpio_put(NODE_RESET_PIN, 0);
    
    // Add entry to the bus activity log
    char activity_entry[128];
    snprintf(activity_entry, sizeof(activity_entry), "Reset all nodes via hardware reset pin | ");
    
    // Add the new entry to bus_activity_log
    size_t current_len = strlen(bus_activity_log);
    size_t entry_len = strlen(activity_entry);
    
    // If adding this entry would exceed ~480 characters (6 lines * 80 chars), clear
    if (current_len + entry_len > 480) {
        // Clear the log completely
        bus_activity_log[0] = '\0';
        bus_activity_rows = 0;
    }
    
    // Add the new entry
    strcat(bus_activity_log, activity_entry);
    bus_activity_rows++;
    
    printf("Node reset complete\n");
}

// Common page header
void generate_page_header(char* buffer, size_t buffer_size, const char* page_title) {
    snprintf(buffer, buffer_size,
        "<html><head><title>%s</title>"
        "<style>"
        "body{font-family:Arial;margin:40px;background:linear-gradient(135deg, #1e3c72 0%%, #2a5298 100%%);color:white;}"
        ".btn{padding:12px 20px;margin:8px;text-decoration:none;border-radius:8px;color:white;font-weight:bold;display:inline-block;min-width:120px;text-align:center;}"
        ".menu-btn{background:#17a2b8;} .green{background:#28a745;} .blue{background:#007bff;} .red{background:#dc3545;} .off{background:#6c757d;} .back{background:#6c757d;}"
        ".btn:hover{opacity:0.8;}"
        ".control-row{margin:15px 0;padding:15px;background:rgba(255,255,255,0.1);border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.2);}"
        "h1{color:white;text-shadow:2px 2px 4px rgba(0,0,0,0.3);} h2{color:#e8f4fd;} h3{color:#b8d9f7;}"
        "p{color:#e8f4fd;} table{color:#e8f4fd;}"
        "</style>"
        "</head><body>"
        "<h1>Z1 Computer Tests - NeuroFab Corp</h1>",
        page_title);
}

// Function to generate LED test page header (with auto-refresh)
void generate_led_page_header(char* buffer, size_t buffer_size, const char* page_title) {
    snprintf(buffer, buffer_size,
        "<html><head><title>%s</title>"
        "<meta http-equiv='refresh' content='2'>"
        "<style>"
        "body{font-family:Arial;margin:40px;background:linear-gradient(135deg, #1e3c72 0%%, #2a5298 100%%);color:white;}"
        ".btn{padding:12px 20px;margin:8px;text-decoration:none;border-radius:8px;color:white;font-weight:bold;display:inline-block;min-width:120px;text-align:center;}"
        ".menu-btn{background:#17a2b8;} .green{background:#28a745;} .blue{background:#007bff;} .red{background:#dc3545;} .off{background:#6c757d;} .back{background:#6c757d;}"
        ".btn:hover{opacity:0.8;}"
        ".control-row{margin:15px 0;padding:15px;background:rgba(255,255,255,0.1);border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.2);}"
        "h1{color:white;text-shadow:2px 2px 4px rgba(0,0,0,0.3);} h2{color:#e8f4fd;} h3{color:#b8d9f7;}"
        "p{color:#e8f4fd;} table{color:#e8f4fd;}"
        "</style>"
        "</head><body>"
        "<h1>Z1 Computer Tests - NeuroFab Corp</h1>",
        page_title);
}

// Function to generate main menu page
void generate_main_menu_page(char* buffer, size_t buffer_size) {
    generate_page_header(buffer, buffer_size, "Z1 Computer Tests");
    
    size_t current_len = strlen(buffer);
    
    // Read PHY configuration register for status display
    uint8_t phycfgr = w5500_read_reg(W5500_PHYCFGR, COMMON_REG_BSB);
    bool link_up = (phycfgr & PHYCFGR_LNK) != 0;
    bool speed_100m = (phycfgr & PHYCFGR_SPD) != 0;
    bool full_duplex = (phycfgr & PHYCFGR_DPX) != 0;
    
    // Display network status at the top of page
    snprintf(buffer + current_len, buffer_size - current_len,
        "<div class='control-row' style='background: %s; color: white;'>"
        "<h3>Network Status</h3>"
        "<p><strong>Link:</strong> %s | <strong>Speed:</strong> %s | <strong>Duplex:</strong> %s</p>"
        "<p><strong>PHY Register:</strong> 0x%02X | <strong>Auto-negotiation:</strong> %s</p>"
        "</div>"
        "<h2>Available Tests:</h2>",
        link_up ? "rgba(40, 167, 69, 0.8)" : "rgba(220, 53, 69, 0.8)",
        link_up ? "UP" : "DOWN",
        speed_100m ? "100 Mbps" : "10 Mbps",
        full_duplex ? "Full Duplex" : "Half Duplex",
        phycfgr,
        (phycfgr & PHYCFGR_OPMD) ? "Enabled" : "Disabled");
    
    // Show discovered nodes status
    current_len = strlen(buffer);
    char node_list[128] = "";
    if (z1_active_node_count > 0) {
        char temp[8];
        bool first = true;
        for (int i = 0; i < 16; i++) {
            if (z1_active_nodes[i]) {
                if (!first) strcat(node_list, ", ");
                sprintf(temp, "%d", i);
                strcat(node_list, temp);
                first = false;
            }
        }
    } else {
        strcpy(node_list, "None discovered");
    }
    
    snprintf(buffer + current_len, buffer_size - current_len,
        "<div class='control-row' style='background: rgba(108, 117, 125, 0.8); color: white;'>"
        "<h3>Z1 Matrix Bus Nodes</h3>"
        "<p><strong>Active Nodes:</strong> %d/16</p>"
        "<p><strong>Node IDs:</strong> %s</p>"
        "</div>"
        "<h2>Available Tests:</h2>",
        z1_active_node_count,
        node_list);
    
    current_len = strlen(buffer);
    snprintf(buffer + current_len, buffer_size - current_len,
        "<div class='control-row' style='display:flex; align-items:center; gap:15px; margin-bottom:15px;'>"
        "<a href='/led_test' class='btn menu-btn' style='flex:0 0 auto;'>LED Test</a>"
        "<p style='margin:0; flex:1;'>Test local GPIO LED functionality</p>"
        "</div>"
        "<div class='control-row' style='display:flex; align-items:center; gap:15px; margin-bottom:15px;'>"
        "<a href='/memory_test' class='btn menu-btn' style='flex:0 0 auto;'>Memory Test</a>"
        "<p style='margin:0; flex:1;'>Test PSRAM memory functionality</p>"
        "</div>"
        "<div class='control-row' style='display:flex; align-items:center; gap:15px; margin-bottom:15px;'>"
        "<a href='/sd_test' class='btn menu-btn' style='flex:0 0 auto;'>SD Card Test</a>"
        "<p style='margin:0; flex:1;'>Test SD card file system operations</p>"
        "</div>"
        "<div class='control-row' style='display:flex; align-items:center; gap:15px; margin-bottom:15px;'>"
        "<a href='/bus_test' class='btn menu-btn' style='flex:0 0 auto;'>Z1 Bus Test</a>"
        "<p style='margin:0; flex:1;'>Test Z1 Computer Bus communication</p>"
        "</div>"
        "<div class='control-row' style='display:flex; align-items:center; gap:15px; margin-bottom:15px;'>"
        "<a href='/16k_test' class='btn menu-btn' style='flex:0 0 auto;'>16K Buffer Test</a>"
        "<p style='margin:0; flex:1;'>Test W5500 Network with chunked 16K transmission</p>"
        "</div>"
        "<p><small>Select a test from the menu above</small></p>"
        "</body></html>");
}

// LED test page
void generate_led_test_page(char* buffer, size_t buffer_size) {
    generate_led_page_header(buffer, buffer_size, "LED Test");
    
    size_t current_len = strlen(buffer);
    snprintf(buffer + current_len, buffer_size - current_len,
        "<h2>Controller LED Test</h2>"
        "<h3>Current Status:</h3>"
        "<p>Green LED (GPIO 44): <strong>%s</strong></p>"
        "<p>Blue LED (GPIO 45): <strong>%s</strong></p>"
        "<p>Red LED (GPIO 46): <strong>%s</strong></p>"
        "<h3>Controls:</h3>"
        "<div class='control-row'>Green LED: <a href='/green_on' class='btn green'>Turn ON</a> <a href='/green_off' class='btn off'>Turn OFF</a></div>"
        "<div class='control-row'>Blue LED: <a href='/blue_on' class='btn blue'>Turn ON</a> <a href='/blue_off' class='btn off'>Turn OFF</a></div>"
        "<div class='control-row'>Red LED: <a href='/red_on' class='btn red'>Turn ON</a> <a href='/red_off' class='btn off'>Turn OFF</a></div>"
        "<div class='control-row'><a href='/main' class='btn back'>Back to Main Menu</a></div>"
        "</body></html>",
        led_green_state ? "ON" : "OFF",
        led_blue_state ? "ON" : "OFF", 
        led_red_state ? "ON" : "OFF");
}

// Memory test page
void generate_memory_test_page(char* buffer, size_t buffer_size) {
    const char* status_color = psram_is_initialized() ? "#2a5298" : "#4d4d4d";
    const char* status_text = psram_is_initialized() ? "#ffffff" : "#ffffff";
    const char* status_msg = psram_is_initialized() ? "READY" : "NOT INIT";
    const char* mode_msg = psram_is_initialized() ? (psram_is_quad_mode() ? "QUAD Mode" : "Single Mode") : "N/A";
    
    // Get current system memory usage
    uint32_t used_ram = get_used_ram_kb();
    uint32_t free_ram = get_free_ram_kb();
    uint32_t total_ram = get_total_ram_kb();
    uint32_t flash_used = get_flash_usage_kb();
    uint32_t total_flash = get_total_flash_mb();
    uint32_t free_flash = get_free_flash_mb();
    
    snprintf(buffer, buffer_size,
        "<!DOCTYPE html>"
        "<html><head>"
        "<title>Memory Tests - Z1 Computer</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background: linear-gradient(135deg, #1e3c72 0%%, #2a5298 100%%); color: white; min-height: 100vh; }"
        ".container { max-width: 800px; margin: 0 auto; background: rgba(255,255,255,0.1); padding: 20px; border-radius: 10px; backdrop-filter: blur(10px); }"
        ".header { text-align: center; margin-bottom: 30px; }"
        ".control-row { background: rgba(0,0,0,0.2); padding: 15px; margin: 10px 0; border-radius: 5px; }"
        ".btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; transition: all 0.3s; }"
        ".test { background: #007bff; color: white; }"
        ".test:hover { background: #0056b3; }"
        ".back { background: #6c757d; color: white; }"
        ".back:hover { background: #545b62; }"
        ".status { padding: 15px; border-radius: 5px; margin: 15px 0; font-size: 18px; }"
        ".memory-info { padding: 15px; background: rgba(255,255,255,0.1); border-radius: 5px; margin: 15px 0; }"
        ".test-section { margin: 20px 0; padding: 15px; background: rgba(255,255,255,0.1); border-radius: 5px; }"
        "h1 { color: white; text-align: center; }"
        "h3 { color: #e8f4fd; }"
        ".mem-bar { background: rgba(255,255,255,0.2); border-radius: 10px; height: 20px; margin: 5px 0; }"
        ".mem-fill { background: #28a745; height: 100%%; border-radius: 10px; transition: width 0.3s; }"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<div class='header'>"
        "<h1>Memory Tests and Status</h1>"
        "<p>PSRAM and System Memory Diagnostics</p>"
        "</div>"
        "<div class='memory-info'>"
        "<h3>System Memory Status</h3>"
        "<strong>RAM (520KB Physical):</strong> %lu KB used, %lu KB free (%.1f%% used)<br>"
        "<div class='mem-bar'><div class='mem-fill' style='width:%.1f%%'></div></div>"
        "<strong>Flash (%lu MB):</strong> %lu KB used, %lu MB free (%.1f%% used)<br>"
        "<div class='mem-bar'><div class='mem-fill' style='width:%.1f%%'></div></div>"
        "<strong>PSRAM:</strong> %.1f MB detected and available"
        "</div>"
        "<div class='test-section'>"
        "<h3>PSRAM Memory Read/Write Test</h3>"
        "<a href='/psram/test/1mb' class='btn test'>1MB Test</a>"
        "<a href='/psram/test/8mb' class='btn test'>8MB Test</a>"
        "</div>"
        "<div class='status' style='background:%s;color:%s'>"
        "PSRAM Status: %s | Size: %zu MB | Mode: %s | Base: 0x%08lX<br>"
        "Last Test: %s<br><br>Press F5 to refresh test status"
        "</div>"
        "<div class='control-row'>"
        "<a href='/main' class='btn back'>Back to Main Menu</a>"
        "</div>"
        "</div>"
        "</body></html>",
        (unsigned long)used_ram, (unsigned long)free_ram, 
        (float)used_ram * 100.0f / (float)total_ram,
        (float)used_ram * 100.0f / (float)total_ram,
        (unsigned long)total_flash, (unsigned long)flash_used, (unsigned long)free_flash,
        (float)flash_used * 100.0f / ((float)total_flash * 1024.0f),
        (float)flash_used * 100.0f / ((float)total_flash * 1024.0f),
        (float)psram_get_size() / (1024.0f * 1024.0f),
        status_color, status_text, status_msg,
        psram_get_size() / (1024*1024), mode_msg, (unsigned long)psram_get_base_address(),
        last_test_status);
}

// 16K buffer test page
void generate_16k_test_page(char* buffer, size_t buffer_size) {
    generate_page_header(buffer, buffer_size, "16K Buffer Test Page");
    
    size_t current_len = strlen(buffer);
    
    // Generate content to fill exactly 16383 bytes (16384 - 1)
    char pattern[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                     "abcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=[]{}|;:,.<>? ";
    size_t pattern_len = strlen(pattern);
    
    // Calculate how much space we need to fill
    size_t header_len = strlen(buffer);
    size_t footer_len = 200; // Estimate for closing tags and footer
    size_t target_content = 16383 - header_len - footer_len;
    
    size_t pos = current_len;
    pos += snprintf(buffer + pos, buffer_size - pos,
        "<h2>16K Buffer Test</h2>"
        "<p>Testing maximum buffer capacity (16383 bytes) with chunked HTTP transmission.</p>"
        "<div class='test-section'>"
        "<h3>Generated Content Pattern</h3>"
        "<pre style='font-family:monospace;font-size:10px;line-height:1.1;overflow-wrap:break-word;'>");
    
    // Fill with repeating pattern to reach target size
    for (size_t i = 0; i < target_content && pos < buffer_size - footer_len; i++) {
        if (i > 0 && i % 80 == 0) {
            pos += snprintf(buffer + pos, buffer_size - pos, "\n");
        }
        if (pos < buffer_size - 2) {
            buffer[pos++] = pattern[i % pattern_len];
        }
    }
    
    snprintf(buffer + pos, buffer_size - pos,
        "</pre></div>"
        "<div class='control-row'>"
        "<a href='/main' class='btn back'>Back to Main Menu</a>"
        "</div>"
        "<p><small>16K Buffer Test Complete - Size: %zu bytes</small></p>"
        "</body></html>", strlen(buffer));
}

// SD Card Test Page Generator
void generate_sd_test_page(char* buffer, size_t buffer_size) {
    // Auto-initialize SD card when entering page
    if (!sd_card_initialized) {
        printf("Auto-initializing SD card...\n");
        sd_card_init();
        load_sd_directory();
    }
    
    snprintf(buffer, buffer_size,
        "<!DOCTYPE html>"
        "<html><head>"
        "<title>SD Card Test - Z1 Computer</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background: linear-gradient(135deg, #1e3c72 0%%, #2a5298 100%%); color: white; min-height: 100vh; }"
        ".container { max-width: 800px; margin: 0 auto; background: rgba(255,255,255,0.1); padding: 20px; border-radius: 10px; backdrop-filter: blur(10px); }"
        ".header { text-align: center; margin-bottom: 30px; }"
        ".control-row { background: rgba(0,0,0,0.2); padding: 15px; margin: 10px 0; border-radius: 5px; }"
        ".btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; transition: all 0.3s; }"
        ".menu-btn { background: #28a745; color: white; }"
        ".menu-btn:hover { background: #218838; }"
        ".off { background: #6c757d; color: white; }"
        ".off:hover { background: #545b62; }"
        ".green { background: #28a745; color: white; }"
        ".green:hover { background: #218838; }"
        ".red { background: #dc3545; color: white; }"
        ".red:hover { background: #c82333; }"
        ".blue { background: #007bff; color: white; }"
        ".blue:hover { background: #0056b3; }"
        ".test { background: #007bff; color: white; }"
        ".test:hover { background: #0056b3; }"
        ".back { background: #6c757d; color: white; }"
        ".back:hover { background: #545b62; }"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<div class='header'>"
        "<h1>SD Card Test</h1>"
        "<p>File System Operations & Status</p>"
        "</div>"
        
        "<div class='control-row'>"
        "<h3>SD Card Status</h3>");
    
    // Add SD card status
    if (sd_card_initialized) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
            "<p style='color: #28a745; font-weight: bold;'>Status: SD Card Mounted and Ready</p>"
            "<p>Files Found: %d</p>", sd_file_count);
    } else {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
            "<p style='color: #dc3545; font-weight: bold;'>Status: SD Card Not Mounted</p>"
            "<p>Insert SD card and try initialization</p>");
    }
    
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "</div>"
        
        "<div class='control-row'>"
        "<h3>SD Card Operations</h3>"
        "<a href='/sd_refresh' class='btn blue'>Refresh SD Card</a>"
        "</div>"
        
        "<div class='control-row'>"
        "<h3>SD Card Performance Tests</h3>"
        "<a href='/sd_test_128k' class='btn test'>128K Write Test</a>"
        "<a href='/sd_test_1mb' class='btn test'>1MB Write Test</a>"
        "<a href='/sd_test_10mb' class='btn test'>10MB Write Test</a>"
        "</div>"
        
        "<div class='status' style='background:#2a5298;color:#ffffff;padding:10px;margin:10px 0;border-radius:5px;'>"
        "<h4>Test Results:</h4>"
        "<p>%s</p>"
        "</div>", sd_test_results);
    
    // Add directory listing
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='control-row'>"
        "<h3>Directory Contents</h3>");
    
    if (sd_card_initialized && sd_file_count > 0) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
            "<table style='width: 100%%; border-collapse: collapse;'>"
            "<tr style='background: #f8f9fa;'><th style='padding: 8px; border: 1px solid #ddd;'>File Name</th>"
            "<th style='padding: 8px; border: 1px solid #ddd;'>Size (bytes)</th></tr>");
        
        for (int i = 0; i < sd_file_count && i < MAX_SD_FILES; i++) {
            snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
                "<tr><td style='padding: 8px; border: 1px solid #ddd;'>%s</td>"
                "<td style='padding: 8px; border: 1px solid #ddd;'>%lu</td></tr>", 
                sd_files[i].name, sd_files[i].size);
        }
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), "</table>");
    } else if (sd_card_initialized) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
            "<p>Directory is empty</p>");
    } else {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
            "<p style='color: #dc3545;'>SD card not mounted - cannot read directory</p>");
    }
    
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "</div>"
        
        "<div class='control-row'>"
        "<a href='/main' class='btn back'>Back to Main Menu</a>"
        "</div>"      
        "</div>"
        "</body></html>");
}

// Z1 Computer Matrix Bus Test Page
void generate_bus_test_page(char* buffer, size_t buffer_size) {
    generate_page_header(buffer, buffer_size, "Z1 Computer Bus Test");
    
    // Prepare node display string
    char node_display[32];
    if (z1_selected_node == Z1_BROADCAST_ADDR) {
        snprintf(node_display, sizeof(node_display), "Broadcast (All Nodes)");
    } else if (z1_selected_node < 12) {
        snprintf(node_display, sizeof(node_display), "Node %d", z1_selected_node);
    } else {
        snprintf(node_display, sizeof(node_display), "Unknown");
    }

    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='header'>"
        "<h2>Z1 Computer Bus Test</h2>"
        "<p>Multi-master 16-bit bus connecting 17 RP2350B nodes</p>"
        "</div>"
        
        "<div class='control-row'>"
        "<h3>Bus Configuration</h3>"
        "<p style='margin: 0; padding: 0;'><strong>Controller ID:</strong> %d</p>"
        "<p style='margin: 0; padding: 0;'><strong>Target Nodes:</strong> 0-15 (16 nodes)</p>"
        "<p style='margin: 0; padding: 0;'><strong>Bus Speed:</strong> %d microsecond clock delay</p>"
        "<p style='margin: 0; padding: 0;'><strong>Bus Status:</strong> %s | <strong>Selected:</strong> %s</p>"
        "</div>",
        Z1_CONTROLLER_ID, z1_bus_clock_high_us, z1_bus_status, node_display);
    
    // Generate highlighted node selection section - only show discovered nodes
    printf("[DEBUG] Generating node buttons with z1_selected_node = %d\n", z1_selected_node);
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='control-row'>"
        "<div style='display: flex; align-items: center; margin-bottom: 10px;'>"
        "<h3 style='margin: 0;'>Target Node</h3>"
        "<div style='width: 100px;'></div>"
        "<a href='/bus_broadcast' class='btn %s' style='margin: 0;'>%s</a>"
        "</div>"
        "<div style='text-align: left; margin: 5px 0;'>",
        (z1_selected_node == Z1_BROADCAST_ADDR) ? "red" : "test",
        (z1_selected_node == Z1_BROADCAST_ADDR) ? "Broadcast All" : "Broadcast All");
    
    // Generate buttons only for discovered nodes
    int nodes_in_row = 0;
    bool row_open = false;
    for (int i = 0; i < 16; i++) {
        if (z1_active_nodes[i]) {
            // Start new row if needed
            if (!row_open) {
                snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
                    "<div style='margin-bottom: 8px;'>");
                row_open = true;
                nodes_in_row = 0;
            }
            
            // Add node button
            snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
                "<a href='/bus_node_%d' class='btn %s' style='margin: 0 6px;'>Node %d</a> ",
                i, (z1_selected_node == i) ? "red" : "test", i);
            
            nodes_in_row++;
            
            // Close row after 6 nodes
            if (nodes_in_row >= 6) {
                snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), "</div>");
                row_open = false;
            }
        }
    }
    
    // Close any open row
    if (row_open) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), "</div>");
    }
    
    // Close node selection section
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), "</div></div>");
    
    // Debug: Print which node should be highlighted
    printf("[DEBUG] Node highlighting: Node %d should be RED, others should be TEST\n", z1_selected_node);
    
    // Add LED control section
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='control-row'>"
        "<h3>LED Control Commands</h3>"
        "<div style='margin: 10px 0;'>"
        "<div style='display: flex; align-items: center; margin: 8px 0; flex-wrap: nowrap;'>"
        "<span style='width: 120px; color: white; flex-shrink: 0;'><strong>Green LED:</strong></span>"
        "<div style='flex-shrink: 0; display: flex; align-items: center;'>"
        "<a href='/bus_green_off' class='btn off' style='margin-right: 8px;'>Off</a>"
        "<input type='range' min='0' max='255' value='0' id='greenSlider' "
        "style='width: 120px; margin-right: 8px;' "
        "onchange='setLED(\"green\", this.value)' "
        "onmouseup='setLED(\"green\", this.value)' "
        "ontouchend='setLED(\"green\", this.value)' "
        "oninput='document.getElementById(\"greenValue\").innerHTML = this.value'>"
        "<span id='greenValue' style='color: #28a745; width: 30px; font-size: 12px;'>0</span>"
        "<a href='/bus_green_max' class='btn green' style='margin-left: 8px;'>Max</a>"
        "<button onclick='runEffect(\"chase\")' class='btn off' style='margin-left: 8px;'>Chase</button>"
        "</div></div>"
        "<div style='display: flex; align-items: center; margin: 8px 0; flex-wrap: nowrap;'>"
        "<span style='width: 120px; color: white; flex-shrink: 0;'><strong>Blue LED:</strong></span>"
        "<div style='flex-shrink: 0; display: flex; align-items: center;'>"
        "<a href='/bus_blue_off' class='btn off' style='margin-right: 8px;'>Off</a>"
        "<input type='range' min='0' max='255' value='0' id='blueSlider' "
        "style='width: 120px; margin-right: 8px;' "
        "onchange='setLED(\"blue\", this.value)' "
        "onmouseup='setLED(\"blue\", this.value)' "
        "ontouchend='setLED(\"blue\", this.value)' "
        "oninput='document.getElementById(\"blueValue\").innerHTML = this.value'>"
        "<span id='blueValue' style='color: #007bff; width: 30px; font-size: 12px;'>0</span>"
        "<a href='/bus_blue_max' class='btn blue' style='margin-left: 8px;'>Max</a>"
        "<button onclick='runEffect(\"rainbow\")' class='btn off' style='margin-left: 8px;'>Rainbow</button>"
        "</div></div>"
        "<div style='display: flex; align-items: center; margin: 8px 0; flex-wrap: nowrap;'>"
        "<span style='width: 120px; color: white; flex-shrink: 0;'><strong>Red LED:</strong></span>"
        "<div style='flex-shrink: 0; display: flex; align-items: center;'>"
        "<a href='/bus_red_off' class='btn off' style='margin-right: 8px;'>Off</a>"
        "<input type='range' min='0' max='255' value='0' id='redSlider' "
        "style='width: 120px; margin-right: 8px;' "
        "onchange='setLED(\"red\", this.value)' "
        "onmouseup='setLED(\"red\", this.value)' "
        "ontouchend='setLED(\"red\", this.value)' "
        "oninput='document.getElementById(\"redValue\").innerHTML = this.value'>"
        "<span id='redValue' style='color: #dc3545; width: 30px; font-size: 12px;'>0</span>"
        "<a href='/bus_red_max' class='btn red' style='margin-left: 8px;'>Max</a>"
        "<button onclick='runEffect(\"random\")' class='btn off' style='margin-left: 8px;'>Random</button>"
        "</div></div>"
        "</div>"
        "<script>"
        "function setLED(color, value) {"
        "  console.log('Setting ' + color + ' LED to value: ' + value);"
        "  var url = '/bus_' + color + '_set_' + value;"
        "  console.log('Sending request to: ' + url);"
        "  fetch(url, {method: 'GET'})"
        "    .then(response => console.log('Response:', response.status))"
        "    .catch(error => console.log('Error:', error));"
        "}"
        "function runEffect(effect) {"
        "  console.log('Running effect: ' + effect);"
        "  var url = '/bus_effect_' + effect;"
        "  console.log('Sending request to: ' + url);"
        "  fetch(url, {method: 'GET'})"
        "    .then(response => console.log('Effect response:', response.status))"
        "    .catch(error => console.log('Effect error:', error));"
        "}"
        "</script>"
        "</div>");
    
    // Add discovered nodes section
    char node_list[128] = "";
    if (z1_active_node_count > 0) {
        char temp[8];
        bool first = true;
        for (int i = 0; i < 16; i++) {
            if (z1_active_nodes[i]) {
                if (!first) strcat(node_list, ", ");
                sprintf(temp, "%d", i);
                strcat(node_list, temp);
                first = false;
            }
        }
    } else {
        strcpy(node_list, "None - Run discovery scan");
    }
    
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='control-row' style='background: rgba(108, 117, 125, 0.6);'>"
        "<h3>Discovered Nodes</h3>"
        "<p><strong>Active:</strong> %d/16 nodes</p>"
        "<p><strong>Node IDs:</strong> %s</p>"
        "<p><strong>Status:</strong> %s</p>"
        "<a href='/rediscover_nodes' class='btn off' style='background-color:#0066cc;'>🔍 Re-scan Nodes</a>"
        "</div>",
        z1_active_node_count,
        node_list,
        z1_node_discovery_status);
    
    // Add Bus Ping Test section
    snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
        "<div class='control-row' style='display:flex; align-items:flex-start; gap:20px;'>"
        "<div style='flex:0 0 auto;'>"
        "<h3>Bus Control</h3>"
        "<a href='/ping_all_nodes' class='btn off'>Ping Test</a> "
        "<a href='/reset_all_nodes' class='btn off' style='background-color:#ff3300;'>Reset All Nodes</a>"
        "</div>"
        "<div style='flex:1; min-width:400px;'>"
        "<h4 style='margin-top:0; color:#00ff00;'>Recent Bus Activity</h4>"
        "<div style='background:#1a1a1a;color:#00ff00;padding:10px;border-radius:5px;font-family:monospace;font-size:12px;height:120px;overflow-y:auto;border:1px solid #333;'>"
        "<div style='color:#00ccff;'>%s</div>"
        "</div>"
        "</div>"
        "</div>"
        
        "<div class='control-row'>"
        "<a href='/main' class='btn back'>Back to Main Menu</a>"
        "</div>"
        "</div>"
        "</body></html>",
        strlen(bus_activity_log) > 0 ? bus_activity_log : "No activity yet...");
}

// Helper function to turn off all LEDs on all nodes
void clear_all_leds() {
    // printf("Clearing all LEDs\n");
    z1_bus_broadcast(0x20, 0x00);  // Red off
    z1_bus_broadcast(0x10, 0x00);  // Green off
    z1_bus_broadcast(0x30, 0x00);  // Blue off
    sleep_ms(50);  // Short delay for stability
}

// Chase pattern function - cycles red, green, blue across discovered nodes only
void run_chase_pattern() {
    // printf("Starting LED chase pattern\n");
    
    // Clear all LEDs first
    clear_all_leds();
    
    // Colors: Red=0x20, Green=0x10, Blue=0x30
    uint8_t colors[] = {0x20, 0x10, 0x30};
    char* color_names[] = {"Red", "Green", "Blue"};
    
    // Count active nodes for timing calculation
    int active_node_count = 0;
    for (int i = 0; i < 16; i++) {
        if (z1_active_nodes[i]) active_node_count++;
    }
    
    if (active_node_count == 0) {
        printf("No active nodes for chase pattern\n");
        return;
    }
    
    int led_count = 0;  // Track total LEDs across all passes
    int total_leds = 3 * 4 * active_node_count;  // 3 colors × 4 passes × active nodes
    
    for (int color = 0; color < 3; color++) {
        // printf("Chase pattern: %s\n", color_names[color]);
        
        for (int pass = 0; pass < 4; pass++) {
            bool turn_on = (pass % 2 == 0);  // Pass 0,2 = ON, Pass 1,3 = OFF
            // printf("  Pass %d (%s)\n", pass + 1, turn_on ? "ON" : "OFF");
            
            // Chase across only discovered nodes
            for (int node = 0; node < 16; node++) {
                if (!z1_active_nodes[node]) continue;  // Skip inactive nodes
                
                // Speed up with every single LED: Start at 15ms, drop to 5ms
                int delay = 15 - (led_count * 10) / (total_leds - 1);
                if (delay < 5) delay = 5;  // Minimum 5ms
                
                led_count++;  // Increment for next LED
                
                // Turn LED on or off based on pass
                if (turn_on) {
                    z1_bus_write(node, colors[color], 0xFF);  // Turn on (stays on)
                } else {
                    z1_bus_write(node, colors[color], 0x00);  // Turn off
                }
                
                // Wait - gets faster with every LED
                if (delay > 0) {
                    sleep_ms(delay);
                }
            }
        }
        
        sleep_ms(25);  // Brief pause between colors
    }
    
    // printf("Chase pattern complete\n");
}

// Random effect - PWM fade in/out with random channel selection (discovered nodes only)
void run_random_effect() {
    // printf("Starting random effect\n");
    
    // Clear all LEDs first
    clear_all_leds();
    
    // Build list of active nodes
    uint8_t active_node_list[16];
    int active_count = 0;
    for (int i = 0; i < 16; i++) {
        if (z1_active_nodes[i]) {
            active_node_list[active_count++] = i;
        }
    }
    
    if (active_count == 0) {
        printf("No active nodes for random effect\n");
        return;
    }
    
    // Colors: Red=0x20, Green=0x10, Blue=0x30
    uint8_t colors[] = {0x20, 0x10, 0x30};
    
    // Run 6 random sets for 0.35 seconds each (~2.1 seconds total)
    for (int set = 0; set < 5; set++) {
        // Select random channels from active nodes only
        uint8_t selected_nodes[16];
        uint8_t selected_colors[16];
        
        // Generate random node/color combinations from active nodes
        int selection_count = (active_count > 16) ? 16 : active_count;
        for (int i = 0; i < selection_count; i++) {
            selected_nodes[i] = active_node_list[rand() % active_count];  // Random active node
            selected_colors[i] = colors[rand() % 3];  // Random color channel
        }
        
        // Fade in for 175ms (14 steps × 12.5ms)
        for (int step = 0; step < 14; step++) {
            int brightness = (step * 255) / 13;  // 0 to 255 in 14 steps
            for (int i = 0; i < selection_count; i++) {
                z1_bus_write(selected_nodes[i], selected_colors[i], brightness);
            }
            sleep_ms(12);  // Half the previous delay for faster cycles
        }
        
        // Fade out for 175ms (14 steps × 12.5ms)  
        for (int step = 0; step < 14; step++) {
            int brightness = 255 - ((step * 255) / 13);  // 255 to 0 in 14 steps
            for (int i = 0; i < selection_count; i++) {
                z1_bus_write(selected_nodes[i], selected_colors[i], brightness);
            }
            sleep_ms(12);  // Half the previous delay for faster cycles
        }
        
        // Clear all before next set
        clear_all_leds();
    }
    
    // Turn off all LEDs after effect completes
    clear_all_leds();
    
    // printf("Random effect complete\n");
}

// Rainbow effect - Complex PWM color mixing and transitions (discovered nodes only)
void run_rainbow_effect() {
    // printf("Starting rainbow effect\n");
    
    // Clear all LEDs first
    clear_all_leds();
    
    // Build list of active nodes
    uint8_t active_node_list[16];
    int active_count = 0;
    for (int i = 0; i < 16; i++) {
        if (z1_active_nodes[i]) {
            active_node_list[active_count++] = i;
        }
    }
    
    if (active_count == 0) {
        printf("No active nodes for rainbow effect\n");
        return;
    }
    
    // Run for 2.25 seconds (25% reduction from 3 seconds) - rotating rainbow wheel
    for (int rotation = 0; rotation < 45; rotation++) {
        for (int idx = 0; idx < active_count; idx++) {
            int node = active_node_list[idx];
            int hue = (rotation * 15 + idx * 30) % 360;
            // Convert HSV to RGB-like values for our 3 colors
            if (hue < 120) {
                z1_bus_write(node, 0x20, 255 - (hue * 2));  // Red
                z1_bus_write(node, 0x10, hue * 2);          // Green
                z1_bus_write(node, 0x30, 0);                // Blue
            } else if (hue < 240) {
                z1_bus_write(node, 0x20, 0);                        // Red
                z1_bus_write(node, 0x10, 255 - ((hue - 120) * 2));  // Green
                z1_bus_write(node, 0x30, (hue - 120) * 2);          // Blue
            } else {
                z1_bus_write(node, 0x20, (hue - 240) * 2);          // Red
                z1_bus_write(node, 0x10, 0);                        // Green
                z1_bus_write(node, 0x30, 255 - ((hue - 240) * 2));  // Blue
            }
        }
        sleep_ms(50);
    }
    
    // Turn off all LEDs after effect completes
    clear_all_leds();
    
    // printf("Rainbow effect complete\n");
}

// HTTP Request Parser
void parse_http_request(const char* request, size_t length) {
    printf("🌍 *** INCOMING HTTP REQUEST *** (length=%zu)\n", length);
    printf("Request content: %.100s\n", request);  // Debug: show first 100 chars
    
    // Update OLED status
    http_connection_active = true;
    update_oled_status("Handling HTTP request");
    
    // Debug: Print the first line specifically for URL parsing
    const char* first_line_end = strstr(request, "\r\n");
    if (first_line_end) {
        size_t first_line_len = first_line_end - request;
        printf("First line: %.*s\n", (int)first_line_len, request);
        
        // Extract the URL for display (only for GET requests)
        char url_display[22] = {0}; // Limited to 21 chars + null terminator
        if (strncmp(request, "GET ", 4) == 0) {
            const char* url_start = request + 4;
            const char* url_end = strchr(url_start, ' ');
            if (url_end) {
                size_t url_len = url_end - url_start;
                if (url_len > 21) url_len = 21;
                strncpy(url_display, url_start, url_len);
                url_display[url_len] = '\0';
                
                char status[64];
                snprintf(status, sizeof(status), "GET %s", url_display);
                update_oled_status(status);
            }
        }
    }
    
    // Reset favicon flag
    is_favicon_request = false;
    
    // Handle favicon requests immediately
    if (strstr(request, "/favicon.ico") != NULL) {
        printf("Favicon request - will send 404\n");
        is_favicon_request = true;
        return; // Just set flag and return
    }
    
    // Check for page navigation first - these take priority
    if (strstr(request, "/main") != NULL) {
        printf("Main menu requested\n");
        current_page = PAGE_MAIN_MENU;
        printf("Page state set to: MAIN_MENU\n");
        return; // Navigation processed
    } 
    
    if (strstr(request, "/ HTTP") != NULL || strstr(request, "GET / ") != NULL) {
        printf("Root page requested\n");
        current_page = PAGE_MAIN_MENU;
        printf("Page state set to: MAIN_MENU\n");
        return; // Navigation processed
    }
    
    if (strstr(request, "/16k_test") != NULL) {
        printf("16K buffer test page requested\n");
        current_page = PAGE_16K_TEST;
        printf("Page state set to: 16K_TEST\n");
        return; // Navigation processed
    }
    
    // SD Card specific operations (check these first before generic /sd_test)
    if (strstr(request, "/sd_test_128k") != NULL) {
        printf("SD card 128K performance test requested\n");
        sd_performance_test("test128k.bin", 0);  // 0 means 128K (handled specially)
        load_sd_directory();  // Refresh directory
        current_page = PAGE_SD_TEST;  // Stay on SD page to show results
        return;
    }
    if (strstr(request, "/sd_test_1mb") != NULL) {
        printf("SD card 1MB performance test requested\n");
        sd_performance_test("test1mb.bin", 1);
        load_sd_directory();  // Refresh directory
        current_page = PAGE_SD_TEST;  // Stay on SD page to show results
        return;
    }
    if (strstr(request, "/sd_test_10mb") != NULL) {
        printf("SD card 10MB performance test requested\n");
        sd_performance_test("test10mb.bin", 10);
        load_sd_directory();  // Refresh directory
        current_page = PAGE_SD_TEST;  // Stay on SD page to show results
        return;
    }

    // SD Card page navigation (generic, comes after specific operations)
    if (strstr(request, "/sd_test") != NULL) {
        printf("SD card test page requested\n");
        current_page = PAGE_SD_TEST;
        printf("Page state set to: SD_TEST\n");
        return; // Navigation processed
    }

    // SD Card operations
    if (strstr(request, "/sd_refresh") != NULL) {
        printf("SD card refresh requested\n");
        sd_card_init();  // Re-initialize to detect changes
        load_sd_directory();  // Reload directory
        current_page = PAGE_SD_TEST;  // Stay on SD page to show results
        return;
    }
    
    // Slider LED control commands - PROCESS THESE FIRST before node selection
    printf("[DEBUG] Checking slider commands for request: %.50s\n", request);
    if (strstr(request, "/bus_green_set_") != NULL) {
        printf("[DEBUG] MATCHED /bus_green_set_ slider command!\n");
        int value = 0;
        sscanf(strstr(request, "/bus_green_set_"), "/bus_green_set_%d", &value);
        printf("[DEBUG] Parsed slider value: %d\n", value);
        if (value >= 0 && value <= 255) {
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                success = z1_bus_broadcast(0x10, value);  // Green LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Green LED Set %d: %s", 
                        value, success ? "SUCCESS" : "FAILED");
            } else {
                success = z1_bus_write(z1_selected_node, 0x10, value);  // Green LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Green LED Set %d on node %d: %s", 
                        value, z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
        }
        // Send simple OK response for AJAX requests, don't generate full page
        printf("Slider command processed, sending simple OK response\n");
        simple_ajax_response = true;
        return;
    }
    if (strstr(request, "/bus_blue_set_") != NULL) {
        printf("[DEBUG] MATCHED /bus_blue_set_ slider command!\n");
        int value = 0;
        sscanf(strstr(request, "/bus_blue_set_"), "/bus_blue_set_%d", &value);
        printf("[DEBUG] Parsed slider value: %d\n", value);
        if (value >= 0 && value <= 255) {
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                success = z1_bus_broadcast(0x30, value);  // Blue LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Blue LED Set %d: %s", 
                        value, success ? "SUCCESS" : "FAILED");
            } else {
                success = z1_bus_write(z1_selected_node, 0x30, value);  // Blue LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Blue LED Set %d on node %d: %s", 
                        value, z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
        }
        // Send simple OK response for AJAX requests, don't generate full page
        printf("Slider command processed, sending simple OK response\n");
        simple_ajax_response = true;
        return;
    }
    if (strstr(request, "/bus_red_set_") != NULL) {
        printf("[DEBUG] MATCHED /bus_red_set_ slider command!\n");
        int value = 0;
        sscanf(strstr(request, "/bus_red_set_"), "/bus_red_set_%d", &value);
        printf("[DEBUG] Parsed slider value: %d\n", value);
        if (value >= 0 && value <= 255) {
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                success = z1_bus_broadcast(0x20, value);  // Red LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Red LED Set %d: %s", 
                        value, success ? "SUCCESS" : "FAILED");
            } else {
                success = z1_bus_write(z1_selected_node, 0x20, value);  // Red LED
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Red LED Set %d on node %d: %s", 
                        value, z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
        }
        // Send simple OK response for AJAX requests, don't generate full page  
        printf("Slider command processed, sending simple OK response\n");
        simple_ajax_response = true;
        return;
    }
    
    // LED Effects commands
    if (strstr(request, "/bus_effect_chase") != NULL) {
        // printf("Chase pattern effect requested\n");
        run_chase_pattern();
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Chase pattern completed successfully");
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_effect_random") != NULL) {
        // printf("Random effect requested\n");
        run_random_effect();
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Random effect completed successfully");
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_effect_rainbow") != NULL) {
        // printf("Rainbow effect requested\n");
        run_rainbow_effect();
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Rainbow effect completed successfully");
        current_page = PAGE_BUS_TEST;
        return;
    }
    
    // Z1 Bus Test operations 
    // Node selection commands - CHECK LONGER PATTERNS FIRST!
    if (strstr(request, "/bus_node_10") != NULL) {
        printf("[HTTP DEBUG] Selecting node 10\n");
        z1_selected_node = 10;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_11") != NULL) {
        printf("[HTTP DEBUG] Selecting node 11\n");
        z1_selected_node = 11;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_12") != NULL) {
        printf("[HTTP DEBUG] Selecting node 12\n");
        z1_selected_node = 12;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_13") != NULL) {
        printf("[HTTP DEBUG] Selecting node 13\n");
        z1_selected_node = 13;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_14") != NULL) {
        printf("[HTTP DEBUG] Selecting node 14\n");
        z1_selected_node = 14;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_15") != NULL) {
        printf("[HTTP DEBUG] Selecting node 15\n");
        z1_selected_node = 15;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_0") != NULL) {
        printf("[HTTP DEBUG] Selecting node 0\n");
        z1_selected_node = 0;
        current_page = PAGE_BUS_TEST;
        printf("[HTTP DEBUG] Node 0 selected, z1_selected_node is now: %d\n", z1_selected_node);
        sleep_ms(100);  // Small delay to ensure state is stable
        return;
    }
    if (strstr(request, "/bus_node_1") != NULL) {
        printf("[HTTP DEBUG] Selecting node 1\n");
        printf("[HTTP DEBUG] Request contains /bus_node_1 pattern: %.50s\n", request);
        z1_selected_node = 1;
        current_page = PAGE_BUS_TEST;
        printf("[HTTP DEBUG] Node 1 selected, z1_selected_node is now: %d\n", z1_selected_node);
        sleep_ms(100);  // Small delay to ensure state is stable
        return;
    }
    if (strstr(request, "/bus_node_2") != NULL) {
        printf("[HTTP DEBUG] Selecting node 2\n");
        z1_selected_node = 2;
        current_page = PAGE_BUS_TEST;
        printf("[HTTP DEBUG] Node 2 selected, z1_selected_node is now: %d\n", z1_selected_node);
        sleep_ms(100);  // Small delay to ensure state is stable
        return;
    }
    if (strstr(request, "/bus_node_3") != NULL) {
        printf("[HTTP DEBUG] Selecting node 3\n");
        z1_selected_node = 3;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_4") != NULL) {
        z1_selected_node = 4;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_5") != NULL) {
        z1_selected_node = 5;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_6") != NULL) {
        z1_selected_node = 6;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_7") != NULL) {
        z1_selected_node = 7;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_8") != NULL) {
        z1_selected_node = 8;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_node_9") != NULL) {
        z1_selected_node = 9;
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_broadcast") != NULL) {
        z1_selected_node = Z1_BROADCAST_ADDR;
        current_page = PAGE_BUS_TEST;
        return;
    }
    
    // LED control commands via Z1 Bus
    printf("[HTTP DEBUG] Checking Z1 bus commands in request: %.150s\n", request);
    if (strstr(request, "/bus_green_off") != NULL) {
        printf("[HTTP DEBUG] Matched /bus_green_off request\n");
        printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            printf("[HTTP DEBUG] Using BROADCAST mode\n");
            success = z1_bus_broadcast(0x10, 0);  // Green LED, brightness 0
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Green LED OFF: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            printf("[HTTP DEBUG] Using TARGETED mode\n");
            success = z1_bus_write(z1_selected_node, 0x10, 0);  // Green LED, brightness 0
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Green LED OFF on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        return;
    }
    if (strstr(request, "/bus_green_on") != NULL) {
        printf("[HTTP DEBUG] Matched /bus_green_on request\n");
        printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            printf("[HTTP DEBUG] Using BROADCAST mode\n");
            success = z1_bus_broadcast(0x10, 128);  // Green LED, brightness 128
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Green LED ON: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            printf("[HTTP DEBUG] Using TARGETED mode\n");
            success = z1_bus_write(z1_selected_node, 0x10, 128);  // Green LED, brightness 128
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Green LED ON on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_green_max") != NULL) {
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            success = z1_bus_broadcast(0x10, 255);  // Green LED, brightness 255
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Green LED MAX: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            success = z1_bus_write(z1_selected_node, 0x10, 255);  // Green LED, brightness 255
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Green LED MAX on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_blue_off") != NULL) {
        printf("[HTTP DEBUG] Matched /bus_blue_off request\n");
        printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            printf("[HTTP DEBUG] Using BROADCAST mode\n");
            success = z1_bus_broadcast(0x30, 0);  // Blue LED, brightness 0
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Blue LED OFF: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            printf("[HTTP DEBUG] Using TARGETED mode\n");
            success = z1_bus_write(z1_selected_node, 0x30, 0);  // Blue LED, brightness 0
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Blue LED OFF on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_blue_on") != NULL) {
        printf("[HTTP DEBUG] Matched /bus_blue_on request\n");
        printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            printf("[HTTP DEBUG] Using BROADCAST mode\n");
            success = z1_bus_broadcast(0x30, 128);  // Blue LED, brightness 128
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Blue LED ON: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            printf("[HTTP DEBUG] Using TARGETED mode\n");
            success = z1_bus_write(z1_selected_node, 0x30, 128);  // Blue LED, brightness 128
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Blue LED ON on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_blue_max") != NULL) {
        bool success;
        if (z1_selected_node == Z1_BROADCAST_ADDR) {
            success = z1_bus_broadcast(0x30, 255);  // Blue LED, brightness 255
            snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Blue LED MAX: %s", 
                    success ? "SUCCESS" : "FAILED");
        } else {
            success = z1_bus_write(z1_selected_node, 0x30, 255);  // Blue LED, brightness 255
            snprintf(z1_bus_status, sizeof(z1_bus_status), "Blue LED MAX on node %d: %s", 
                    z1_selected_node, success ? "SUCCESS" : "FAILED");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    // Extract the URL path from the request for exact matching
    char url_path[256];
    if (sscanf(request, "GET %255s HTTP", url_path) == 1) {
        printf("[HTTP DEBUG] Extracted URL path: '%s'\n", url_path);
        
        // Red LED commands - use exact path matching
        if (strcmp(url_path, "/bus_red_off") == 0) {
            printf("[HTTP DEBUG] Matched /bus_red_off request\n");
            printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                printf("[HTTP DEBUG] Using BROADCAST mode\n");
                success = z1_bus_broadcast(0x20, 0);  // Red LED, brightness 0
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Red LED OFF: %s", 
                        success ? "SUCCESS" : "FAILED");
            } else {
                printf("[HTTP DEBUG] Using TARGETED mode\n");
                success = z1_bus_write(z1_selected_node, 0x20, 0);  // Red LED, brightness 0
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Red LED OFF on node %d: %s", 
                        z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
            current_page = PAGE_BUS_TEST;
            return;
        }
        if (strcmp(url_path, "/bus_red_on") == 0) {
            printf("[HTTP DEBUG] Matched /bus_red_on request\n");
            printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                printf("[HTTP DEBUG] Using BROADCAST mode\n");
                success = z1_bus_broadcast(0x20, 128);  // Red LED, brightness 128
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Red LED ON: %s", 
                        success ? "SUCCESS" : "FAILED");
            } else {
                printf("[HTTP DEBUG] Using TARGETED mode\n");
                success = z1_bus_write(z1_selected_node, 0x20, 128);  // Red LED, brightness 128
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Red LED ON on node %d: %s", 
                        z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
            current_page = PAGE_BUS_TEST;
            return;
        }
        if (strcmp(url_path, "/bus_red_max") == 0) {
            printf("[HTTP DEBUG] Matched /bus_red_max request\n");
            printf("[HTTP DEBUG] z1_selected_node = %d\n", z1_selected_node);
            bool success;
            if (z1_selected_node == Z1_BROADCAST_ADDR) {
                printf("[HTTP DEBUG] Using BROADCAST mode\n");
                success = z1_bus_broadcast(0x20, 255);  // Red LED, brightness 255
                snprintf(z1_bus_status, sizeof(z1_bus_status), "BROADCAST Red LED MAX: %s", 
                        success ? "SUCCESS" : "FAILED");
            } else {
                printf("[HTTP DEBUG] Using TARGETED mode\n");
                success = z1_bus_write(z1_selected_node, 0x20, 255);  // Red LED, brightness 255
                snprintf(z1_bus_status, sizeof(z1_bus_status), "Red LED MAX on node %d: %s", 
                        z1_selected_node, success ? "SUCCESS" : "FAILED");
            }
            current_page = PAGE_BUS_TEST;
            return;
        }
    }
    
    // Bus test operations
    if (strstr(request, "/bus_test_sequence") != NULL) {
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Running LED sequence test...");
        // Simple sequence: turn all LEDs off, then cycle through colors
        for (int node = 0; node < 6; node++) {
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
            sleep_ms(100);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, Z1_LED_GREEN);
            sleep_ms(200);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, Z1_LED_GREEN | Z1_LED_RED);
            sleep_ms(200);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, Z1_LED_RED);
            sleep_ms(200);
            z1_bus_write(node, Z1_CMD_LED_CONTROL, 0);
        }
        snprintf(z1_bus_status, sizeof(z1_bus_status), "LED sequence test completed on 6 nodes");
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/rediscover_nodes") != NULL) {
        printf("Re-scan Nodes button pressed\n");
        snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), "Scanning... (48s)");
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Node discovery in progress - sequential scan starting...");
        
        // Execute sequential node discovery
        bool success = z1_discover_nodes_sequential(z1_active_nodes);
        
        if (success) {
            // Count active nodes
            z1_active_node_count = 0;
            for (int i = 0; i < 16; i++) {
                if (z1_active_nodes[i]) z1_active_node_count++;
            }
            snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), "Found %d nodes", z1_active_node_count);
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✓ Discovery complete - found %d/16 nodes", z1_active_node_count);
        } else {
            snprintf(z1_node_discovery_status, sizeof(z1_node_discovery_status), "Scan failed");
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✗ Node discovery failed");
        }
        
        current_page = PAGE_BUS_TEST;
        return;
    }
    
    if (strstr(request, "/ping_all_nodes") != NULL) {
        printf("Ping Test button pressed - pinging discovered nodes only\n");
        
        // Count active nodes
        int active_count = 0;
        for (int i = 0; i < 16; i++) {
            if (z1_active_nodes[i]) active_count++;
        }
        
        if (active_count == 0) {
            snprintf(z1_bus_status, sizeof(z1_bus_status), "⚠️ No active nodes discovered - run discovery first");
            current_page = PAGE_BUS_TEST;
            return;
        }
        
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Pinging %d discovered node(s)...", active_count);
        
        // Clear ping history before sending new pings
        z1_bus_clear_ping_history();
        
        // Ping only discovered nodes
        int ping_count = 0;
        for (int node = 0; node < 16; node++) {
            if (z1_active_nodes[node]) {
                if (z1_bus_ping_node(node)) {
                    ping_count++;
                }
            }
        }
        
        if (ping_count > 0) {
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✓ Pinged %d node(s) - check activity monitor for responses", ping_count);
        } else {
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✗ Failed to send pings");
        }
        
        current_page = PAGE_BUS_TEST;
        return;
    }
    
    if (strstr(request, "/reset_all_nodes") != NULL) {
        printf("Reset All Nodes button pressed\n");
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Resetting all nodes...");
        
        // Execute node reset function
        reset_nodes();
        
        snprintf(z1_bus_status, sizeof(z1_bus_status), "✓ All nodes have been reset");
        
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_test_50percent") != NULL) {
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Testing new protocol: Node 1 LED 50%...");
        
        // Test your specific scenario: send to node 1, command 0x10, data 0x7F (50%)
        printf("[TEST] Testing new protocol: Sending to node 1, cmd=0x10, data=0x7F (50%%)\n");
        bool success = z1_bus_write(1, 0x10, 0x7F);
        
        if (success) {
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✓ New protocol test successful: Node 1 LED set to 50%%");
        } else {
            snprintf(z1_bus_status, sizeof(z1_bus_status), "✗ New protocol test failed: Node 1 did not respond");
        }
        current_page = PAGE_BUS_TEST;
        return;
    }
    if (strstr(request, "/bus_reset") != NULL) {
        z1_bus_release();
        snprintf(z1_bus_status, sizeof(z1_bus_status), "Bus reset - all pins released to high-Z");
        current_page = PAGE_BUS_TEST;
        return;
    }
    
    // Z1 Bus Test page navigation (generic, comes after specific bus operations)  
    if (strstr(request, "/bus_test") != NULL) {
        printf("Z1 Bus test page requested (generic)\n");
        current_page = PAGE_BUS_TEST;
        printf("Page state set to: BUS_TEST\n");
        return; // Navigation processed
    }
    
    // PSRAM test commands - ONLY 1MB and 8MB tests
    if (strstr(request, "/psram/test/1mb") != NULL) {
        printf("PSRAM 1MB test requested\n");
        test_psram_memory(1024 * 1024);  // 1MB
        return;
    }
    if (strstr(request, "/psram/test/8mb") != NULL) {
        printf("PSRAM 8MB test requested\n");
        test_psram_memory(8 * 1024 * 1024);  // 8MB
        return;
    }
    if (strstr(request, "/psram/init") != NULL) {
        printf("PSRAM initialization requested\n");
        psram_init();
        return;
    }
    
    // LED control commands (only process if we're on the LED test page)
    if (current_page == PAGE_LED_TEST) {
        if (strstr(request, "/green_on") != NULL || strstr(request, "green_on") != NULL) {
            printf("Green LED ON command received\n");
            set_led(0, true);
        } else if (strstr(request, "/green_off") != NULL || strstr(request, "green_off") != NULL) {
            printf("Green LED OFF command received\n");
            set_led(0, false);
        } else if (strstr(request, "/blue_on") != NULL || strstr(request, "blue_on") != NULL) {
            printf("Blue LED ON command received\n");
            set_led(1, true);
        } else if (strstr(request, "/blue_off") != NULL || strstr(request, "blue_off") != NULL) {
            printf("Blue LED OFF command received\n");
            set_led(1, false);
        } else if (strstr(request, "/red_on") != NULL || strstr(request, "red_on") != NULL) {
            printf("Red LED ON command received\n");
            set_led(2, true);
        } else if (strstr(request, "/red_off") != NULL || strstr(request, "red_off") != NULL) {
            printf("Red LED OFF command received\n");
            set_led(2, false);
        } else {
            printf("No LED command found in request\n");
        }
    }
    
    // LED test page navigation (generic, comes after specific LED operations)
    if (strstr(request, "/led_test") != NULL) {
        printf("LED test page requested (generic)\n");
        current_page = PAGE_LED_TEST;
        printf("Page state set to: LED_TEST\n");
        return; // Navigation processed
    }
    
    // Memory test page navigation (generic, comes after specific PSRAM operations)
    if (strstr(request, "/memory_test") != NULL) {
        printf("Memory test page requested (generic)\n");
        current_page = PAGE_MEMORY_TEST;
        printf("Page state set to: MEMORY_TEST\n");
        return; // Navigation processed
    }
    
    // Final fallback
    printf("Request processed: navigation or other command\n");
}

const char* get_socket_status_name(uint8_t status) {
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
        default:                    return "UNKNOWN";
    }
}

bool setup_tcp_server(uint16_t port) {
    printf("Setting up TCP server on port %d\n", port);
    
    // 1. Close socket first
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
    sleep_ms(50);
    
    // 2. Clear interrupt register
    w5500_write_reg(S0_IR, SOCKET0_REG_BSB, 0xFF);
    sleep_ms(10);
    
    // 3. Wait for closed state
    int timeout = 50;
    uint8_t status;
    do {
        status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        sleep_ms(10);
        timeout--;
    } while (status != SOCK_STAT_CLOSED && timeout > 0);
    
    if (status != SOCK_STAT_CLOSED) {
        printf("❌ Failed to close socket: 0x%02X\n", status);
        return false;
    }
    
    printf("✅ Socket closed\n");
    
    // 4. Set socket mode to TCP
    w5500_write_reg(S0_MR, SOCKET0_REG_BSB, SOCK_TCP);
    sleep_ms(10);
    
    // 5. Set port
    w5500_write_reg(S0_PORT0, SOCKET0_REG_BSB, (port >> 8) & 0xFF);
    w5500_write_reg(S0_PORT0 + 1, SOCKET0_REG_BSB, port & 0xFF);
    sleep_ms(10);
    
    printf("✅ Set mode=TCP, port=%d\n", port);
    
    // 6. Open socket
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_OPEN);
    
    // Wait for INIT state
    timeout = 100;
    do {
        status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        sleep_ms(10);
        timeout--;
    } while (status != SOCK_STAT_INIT && timeout > 0);
    
    if (status != SOCK_STAT_INIT) {
        printf("❌ Failed to reach INIT state: 0x%02X (%s)\n", status, get_socket_status_name(status));
        return false;
    }
    
    printf("✅ Socket in INIT state\n");
    
    // 7. Start listening
    printf("Starting LISTEN mode...\n");
    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_LISTEN);
    
    // Wait for LISTEN state
    timeout = 100;
    do {
        status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        sleep_ms(10);
        timeout--;
    } while (status != SOCK_STAT_LISTEN && timeout > 0);
    
    if (status != SOCK_STAT_LISTEN) {
        printf("❌ Failed to reach LISTEN state: 0x%02X (%s)\n", status, get_socket_status_name(status));
        return false;
    }
    
    printf("✅ Socket in LISTEN state - ready for connections!\n");
    return true;
}

void monitor_connections(void) {
    printf("Monitoring TCP connections on 192.168.1.222:80\n");
    
    uint8_t last_status = 0xFF;
    uint32_t connection_count = 0;
    
    // Initial network status check 
    check_network_status();
    
    while (true) {
        // No more periodic network status checking
        uint8_t status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
        uint8_t interrupt = w5500_read_reg(S0_IR, SOCKET0_REG_BSB);
        
        // Print status changes
        if (status != last_status) {
            printf("[%08lu] Status change: %s (0x%02X)", to_ms_since_boot(get_absolute_time()), 
                   get_socket_status_name(status), status);
            
            if (interrupt != 0) {
                printf(" [IRQ: 0x%02X]", interrupt);
            }
            printf("\n");
            
            // Update OLED display with socket status
            if (status == SOCK_STAT_ESTABLISHED) {
                http_connection_active = true;
                update_oled_status("HTTP connection open");
            } else if (status == SOCK_STAT_LISTEN) {
                http_connection_active = false;
                update_oled_status("Listening on port 80");
            } else if (status == SOCK_STAT_CLOSED) {
                http_connection_active = false;
                update_oled_status("Socket closed");
            }
            
            last_status = status;
        }
        // Clear any pending interrupts first
        if (interrupt != 0) {
            w5500_write_reg(S0_IR, SOCKET0_REG_BSB, interrupt);  // Clear interrupts
        }
        
        // Handle different states
        switch (status) {
            case SOCK_STAT_ESTABLISHED:
                // Reset only the favicon flag for new connection (preserve page state)
                is_favicon_request = false;
                
                // Check immediately if there's data to receive (no waiting)
                uint8_t rx_size_high = w5500_read_reg(0x0026, SOCKET0_REG_BSB);  // S0_RX_RSR0
                uint8_t rx_size_low = w5500_read_reg(0x0027, SOCKET0_REG_BSB);   // S0_RX_RSR1
                uint16_t rx_size = (rx_size_high << 8) | rx_size_low;
                
                // If no data immediately available, wait very briefly and check once more
                if (rx_size == 0) {
                    sleep_ms(5);  // Very short wait
                    rx_size_high = w5500_read_reg(0x0026, SOCKET0_REG_BSB);
                    rx_size_low = w5500_read_reg(0x0027, SOCKET0_REG_BSB);
                    rx_size = (rx_size_high << 8) | rx_size_low;
                    
                    // If still no data, immediately close - this is a phantom connection
                    if (rx_size == 0) {
                        printf("PHANTOM CONNECTION - immediate close (no data after 5ms)\n");
                        w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                        sleep_ms(10);
                        w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                        break;
                    }
                }                // If still no data after waiting, close immediately (stale connection)
                if (rx_size == 0) {
                    printf("No data received after timeout - forcing immediate closure\n");
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                    sleep_ms(10);
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                    break;
                }
                

                if (rx_size > 0) {
                    printf("🎉 Connection #%lu: Received %d bytes\n", ++connection_count, rx_size);
                    
                    // Read the HTTP request data - ensure we consume ALL data
                    static char request_buffer[1024];  // Increased buffer size
                    memset(request_buffer, 0, sizeof(request_buffer));  // Clear buffer
                    uint16_t read_size = (rx_size > 1023) ? 1023 : rx_size;
                    
                    // Read data from RX buffer
                    uint8_t rx_rd_high = w5500_read_reg(0x0028, SOCKET0_REG_BSB);  // S0_RX_RD0
                    uint8_t rx_rd_low = w5500_read_reg(0x0029, SOCKET0_REG_BSB);   // S0_RX_RD1
                    uint16_t rx_rd_ptr = (rx_rd_high << 8) | rx_rd_low;
                    
                    for (uint16_t i = 0; i < read_size; i++) {
                        uint16_t addr = 0x8000 + ((rx_rd_ptr + i) & 0x1FFF);  // RX buffer mask
                        request_buffer[i] = w5500_read_reg(addr, 0x18);  // Socket 0 RX buffer BSB
                    }
                    request_buffer[read_size] = '\0';
                    
                    // Always consume ALL remaining data from RX buffer to prevent stale requests
                    rx_rd_ptr += rx_size;  // Advance by FULL rx_size, not just read_size
                    w5500_write_reg(0x0028, SOCKET0_REG_BSB, (rx_rd_ptr >> 8) & 0xFF);
                    w5500_write_reg(0x0029, SOCKET0_REG_BSB, rx_rd_ptr & 0xFF);
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_RECV);
                    
                    printf("HTTP Request: %.100s...\n", request_buffer);
                    printf("[HTTP DEBUG] Full request first 200 chars: %.200s\n", request_buffer);
                    
                    // Validate request is complete (has HTTP end marker)
                    if (strstr(request_buffer, "\r\n\r\n") != NULL || strstr(request_buffer, "\n\n") != NULL) {
                        // Parse the HTTP request for LED commands
                        parse_http_request(request_buffer, read_size);
                        
                        printf("After parsing - current_page: %d, z1_selected_node: %d\n", current_page, z1_selected_node);
                        
                        // Small delay to ensure LED state change is registered
                        sleep_ms(10);
                    } else {
                        printf("[HTTP WARNING] Incomplete request received, ignoring\n");
                        // Close connection for incomplete requests
                        w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                        break;
                    }
                    
                    // Send HTTP response
                    printf("Sending HTTP response...\n");
                
                static char http_response[20480];  // Increased response buffer for large pages (20KB)
                
                // Handle favicon requests with simple 404
                if (is_favicon_request) {
                    snprintf(http_response, sizeof(http_response),
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 9\r\n"
                        "\r\n"
                        "Not Found");
                    
                    // Send simple response without chunking
                    uint16_t response_len = strlen(http_response);
                    printf("Sending 404 for favicon: %d bytes\n", response_len);
                    
                    // Get current TX write pointer
                    uint8_t tx_wr_high = w5500_read_reg(0x0024, SOCKET0_REG_BSB);
                    uint8_t tx_wr_low = w5500_read_reg(0x0025, SOCKET0_REG_BSB);
                    uint16_t tx_wr_ptr = (tx_wr_high << 8) | tx_wr_low;
                    
                    // Write response to TX buffer
                    for (uint16_t i = 0; i < response_len; i++) {
                        uint16_t addr = (tx_wr_ptr + i) & 0x07FF;
                        w5500_write_reg(addr, 0x14, http_response[i]);
                    }
                    
                    // Update TX write pointer and send
                    tx_wr_ptr += response_len;
                    w5500_write_reg(0x0024, SOCKET0_REG_BSB, (tx_wr_ptr >> 8) & 0xFF);
                    w5500_write_reg(0x0025, SOCKET0_REG_BSB, tx_wr_ptr & 0xFF);
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_SEND);
                    
                    printf("Favicon 404 sent, closing connection...\n");
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                    break;
                }
                
                // Generate page content
                static char html_body[16384];   // Increased HTML buffer to support up to 16KB pages
                
                // Check if this is a simple AJAX request (slider command)
                if (simple_ajax_response) {
                    printf("Sending simple AJAX response for slider command\n");
                    
                    // Send minimal OK response for AJAX requests
                    const char* simple_response = "HTTP/1.0 200 OK\r\n"
                                                 "Content-Type: text/plain\r\n"
                                                 "Content-Length: 2\r\n"
                                                 "Cache-Control: no-cache\r\n"
                                                 "Connection: close\r\n"
                                                 "\r\n"
                                                 "OK";
                    
                    uint16_t response_len = strlen(simple_response);
                    printf("Sending simple AJAX response: %d bytes\n", response_len);
                    
                    // Send the simple response
                    if (send_raw_data(simple_response, response_len)) {
                        printf("Simple AJAX response sent successfully\n");
                    } else {
                        printf("ERROR: Failed to send simple AJAX response\n");
                    }
                    
                    // Reset the flag and close connection
                    simple_ajax_response = false;
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                    break; // Skip page generation and go to next request
                }
                
                printf("Current page state before generation: %d\n", current_page);
                printf("Selected Z1 node before generation: %d\n", z1_selected_node);
                switch (current_page) {
                    case PAGE_MAIN_MENU:
                        generate_main_menu_page(html_body, sizeof(html_body));
                        printf("Generated main menu page\n");
                        break;
                    case PAGE_LED_TEST:
                        generate_led_test_page(html_body, sizeof(html_body));
                        printf("Generated LED test page\n");
                        break;
                    case PAGE_MEMORY_TEST:
                        generate_memory_test_page(html_body, sizeof(html_body));
                        printf("Generated memory test page\n");
                        break;
                    case PAGE_16K_TEST:
                        generate_16k_test_page(html_body, sizeof(html_body));
                        printf("Generated 16K test page\n");
                        break;
                    case PAGE_SD_TEST:
                        generate_sd_test_page(html_body, sizeof(html_body));
                        printf("Generated SD card test page\n");
                        break;
                    case PAGE_BUS_TEST:
                        generate_bus_test_page(html_body, sizeof(html_body));
                        printf("Generated Z1 Bus test page\n");
                        break;
                    default:
                        generate_main_menu_page(html_body, sizeof(html_body));
                        current_page = PAGE_MAIN_MENU;
                        printf("Default: Generated main menu page\n");
                        break;
                }
                
                
                printf("HTML body length: %zu bytes\n", strlen(html_body));
                
                // Check if we need chunked encoding (for responses > 2KB)
                if (strlen(html_body) > 2000) {
                    // Use proper HTTP/1.1 chunked transfer encoding
                    printf("Large response, using HTTP/1.1 chunked encoding\n");
                    if (send_chunked_http_response(html_body, strlen(html_body))) {
                        printf("HTTP chunked response sent successfully\n");
                    } else {
                        printf("ERROR: Failed to send HTTP chunked response\n");
                    }
                } else {
                    // Small response - send normally with Content-Length
                    printf("Small response, sending with Content-Length\n");
                    
                    // Calculate content length
                    static char content_length_str[16];
                    static uint32_t small_request_counter = 0;
                    small_request_counter++;
                    
                    snprintf(content_length_str, sizeof(content_length_str), "%zu", strlen(html_body));
                    
                    // Build complete HTTP response with calculated content length and strong anti-caching
                    snprintf(http_response, sizeof(http_response),
                        "HTTP/1.0 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: no-cache, no-store, must-revalidate, max-age=0, private\r\n"
                        "Pragma: no-cache\r\n"
                        "Expires: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
                        "Last-Modified: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
                        "ETag: \"small-%u-%u\"\r\n"
                        "Vary: *\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Content-Length: %s\r\n"
                        "\r\n"
                        "%s", small_request_counter, (uint32_t)time_us_32(), content_length_str, html_body);
                    
                    uint16_t response_len = strlen(http_response);
                    printf("Total HTTP response length: %d bytes (with anti-caching ETag: small-%u-%u)\n", response_len, small_request_counter, (uint32_t)time_us_32());
                    
                    // Send entire response at once
                    printf("Sending HTTP response: %d bytes (simple transmission)\n", response_len);
                    
                    // Get current TX write pointer
                    uint8_t tx_wr_high = w5500_read_reg(0x0024, SOCKET0_REG_BSB);
                    uint8_t tx_wr_low = w5500_read_reg(0x0025, SOCKET0_REG_BSB);
                    uint16_t tx_wr_ptr = (tx_wr_high << 8) | tx_wr_low;
                    
                    // Write response to TX buffer
                    for (uint16_t i = 0; i < response_len; i++) {
                        uint16_t addr = (tx_wr_ptr + i) & 0x07FF;
                        w5500_write_reg(addr, 0x14, http_response[i]);
                    }
                    
                    // Update TX write pointer
                    tx_wr_ptr += response_len;
                    w5500_write_reg(0x0024, SOCKET0_REG_BSB, (tx_wr_ptr >> 8) & 0xFF);
                    w5500_write_reg(0x0025, SOCKET0_REG_BSB, tx_wr_ptr & 0xFF);
                    
                    // Send response
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_SEND);
                    
                    // Wait for send to complete
                    int timeout = 200;
                    uint8_t cmd_status;
                    do {
                        cmd_status = w5500_read_reg(S0_CR, SOCKET0_REG_BSB);
                        sleep_ms(5);
                        timeout--;
                    } while (cmd_status != 0 && timeout > 0);
                    
                    if (timeout == 0) {
                        printf("ERROR: Timeout sending HTTP response\n");
                    } else {
                        printf("HTTP response sent successfully\n");
                    }
                }
                
                // Force immediate connection closure for all responses
                printf("Forcing immediate connection closure...\n");
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_DISCON);
                sleep_ms(50);
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                
                // No need for additional close here - already handled above
                }
                break;
                
            case SOCK_STAT_FIN_WAIT:
                printf("FIN_WAIT - connection closing gracefully...\n");
                // Wait briefly for natural close, then force if needed
                sleep_ms(100);
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_CLOSING:
                printf("CLOSING - finalizing connection close...\n");
                sleep_ms(50);
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_TIME_WAIT:
                printf("TIME_WAIT - waiting for timeout...\n");
                sleep_ms(100);
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_CLOSE_WAIT:
                printf("Connection closed by client, cleaning up...\n");
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_CLOSE);
                break;
                
            case SOCK_STAT_LAST_ACK:
                printf("LAST_ACK - waiting for final ACK...\n");
                sleep_ms(50);
                break;
                
            case SOCK_STAT_CLOSED:
                printf("Client disconnected, resuming LISTEN mode...\n");
                
                // Clear any pending interrupts and state
                w5500_write_reg(S0_IR, SOCKET0_REG_BSB, 0xFF);  // Clear all interrupts
                
                // Instead of full restart, just reopen and listen
                w5500_write_reg(S0_MR, SOCKET0_REG_BSB, SOCK_TCP);
                sleep_ms(10);
                w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_OPEN);
                
                // Wait for INIT state
                int timeout = 50;
                uint8_t init_status;
                do {
                    init_status = w5500_read_reg(S0_SR, SOCKET0_REG_BSB);
                    sleep_ms(10);
                    timeout--;
                } while (init_status != SOCK_STAT_INIT && timeout > 0);
                
                if (init_status == SOCK_STAT_INIT) {
                    // Start listening again
                    w5500_write_reg(S0_CR, SOCKET0_REG_BSB, SOCK_LISTEN);
                    printf("✅ Back in LISTEN mode - ready for next connection\n");
                } else {
                    printf("❌ Failed to return to INIT, full restart needed\n");
                    setup_tcp_server(80);
                }
                break;
        }
        
        // Clear any interrupts
        if (interrupt != 0) {
            w5500_write_reg(S0_IR, SOCKET0_REG_BSB, interrupt);
        }
        
        // Poll Z1 Computer Matrix Bus for incoming commands
        z1_bus_handle_interrupt();

        
        sleep_ms(50);  // Faster polling for better responsiveness
    }
}

// Z1 Bus callback function - controller node implementation
void z1_bus_process_command(uint8_t command, uint8_t data) {
    printf("[🎛️ Controller] === BUS ACTIVITY MONITOR ===\n");
    printf("[🎛️ Controller] Received command: 0x%02X, data: %d (0x%02X)\n", command, data, data);
    printf("[🎛️ Controller] From sender ID: %d\n", z1_last_sender_id);
    
    // Decode and log the command for monitoring
    switch (command) {
        case Z1_CMD_RED_LED:
            printf("[🎛️ Controller] 🔴 RED LED command from node %d: PWM=%d/255\n", z1_last_sender_id, data);
            break;
        case Z1_CMD_GREEN_LED:
            printf("[🎛️ Controller] 🟢 GREEN LED command from node %d: PWM=%d/255\n", z1_last_sender_id, data);
            break;
        case Z1_CMD_BLUE_LED:
            printf("[🎛️ Controller] 🔵 BLUE LED command from node %d: PWM=%d/255\n", z1_last_sender_id, data);
            break;
        case Z1_CMD_LED_CONTROL:
            printf("[🎛️ Controller] 💡 LED CONTROL from node %d: pattern=0x%02X\n", z1_last_sender_id, data);
            break;
        case Z1_CMD_PING:
            printf("[🎛️ Controller] 🏓 PING RESPONSE from node %d: data=0x%02X\n", z1_last_sender_id, data);
            // Check if this matches a ping we sent
            z1_bus_handle_ping_response(z1_last_sender_id, data);
            break;
        default:
            printf("[🎛️ Controller] ❓ UNKNOWN command 0x%02X from node %d\n", command, z1_last_sender_id);
            break;
    }
    
    // Log to global bus activity buffer for web interface display
    char activity_entry[128];
    if (command == Z1_CMD_PING) {
        snprintf(activity_entry, sizeof(activity_entry), 
                 "Node %d: Ping Received Data=0x%02X | ", z1_last_sender_id, data);
    } else {
        snprintf(activity_entry, sizeof(activity_entry), 
                 "Node %d: Cmd=0x%02X Data=0x%02X | ", z1_last_sender_id, command, data);
    }
    
    // Simple length-based clearing - approximate 6 lines worth of text
    // Assuming roughly 80 chars per line in the display area
    size_t current_len = strlen(bus_activity_log);
    size_t entry_len = strlen(activity_entry);
    
    // If adding this entry would exceed ~480 characters (6 lines * 80 chars), clear
    if (current_len + entry_len > 480) {
        // Clear the log completely
        bus_activity_log[0] = '\0';
        bus_activity_rows = 0;
    }
    
    // Add the new entry
    strcat(bus_activity_log, activity_entry);
    bus_activity_rows++;
    
    printf("[🎛️ Controller] === END BUS MONITOR ===\n");
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("W5500 TCP Server Starting\n");
    
    if (!init_hardware()) {
        printf("❌ Hardware initialization failed!\n");
        return 1;
    }
    
    // Initialize OLED Display
    printf("Initializing OLED Display...\n");
    ssd1306_init(OLED_I2C, OLED_SDA_PIN, OLED_SCL_PIN);
    printf("OLED: Init function completed\n");
    
    // Now that we can communicate with the OLED, enable it for use
    printf("OLED: Setting initialized flag to true for display usage\n");
    oled_initialized = true;  // Enable OLED usage
    
    printf("Continuing with network setup...\n");
    
    setup_network();
    update_oled_status("Network ready");
    
    if (!setup_tcp_server(80)) {
        printf("❌ TCP server setup failed!\n");
        update_oled_status("TCP server failed!");
        return 1;
    }
    update_oled_status("HTTP server ready");
    
    monitor_connections();
    
    return 0;
}
