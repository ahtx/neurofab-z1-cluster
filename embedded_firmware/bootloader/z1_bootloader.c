/**
 * Z1 Bootloader Implementation
 * 
 * Generic bootloader for NeuroFab Z1 compute nodes.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#include "z1_bootloader.h"
#include <string.h>

// ============================================================================
// Global State
// ============================================================================

static z1_boot_config_t g_boot_config;
static z1_firmware_header_t g_firmware_buffer_header;
static uint32_t g_firmware_upload_offset = 0;

// ============================================================================
// CRC32 Implementation
// ============================================================================

static const uint32_t crc32_table[256] = {
    // Standard CRC32 table (IEEE 802.3)
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    // ... (full table would be here)
};

uint32_t z1_bootloader_crc32(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return ~crc;
}

// ============================================================================
// Boot Configuration
// ============================================================================

static void load_boot_config(void) {
    // Read boot config from flash
    memcpy(&g_boot_config, (void*)BOOT_FLAG_ADDR, sizeof(z1_boot_config_t));
    
    // Initialize if magic is invalid
    if (g_boot_config.magic != BOOT_FLAG_MAGIC) {
        memset(&g_boot_config, 0, sizeof(z1_boot_config_t));
        g_boot_config.magic = BOOT_FLAG_MAGIC;
        g_boot_config.boot_mode = BOOT_MODE_APPLICATION;
        g_boot_config.app_valid = 0;
    }
}

static void save_boot_config(void) {
    // Erase and write boot config to flash
    // (Implementation would use RP2350 flash API)
    // flash_range_erase(BOOT_FLAG_ADDR - FLASH_BASE, 4096);
    // flash_range_program(BOOT_FLAG_ADDR - FLASH_BASE, 
    //                     (uint8_t*)&g_boot_config, 
    //                     sizeof(z1_boot_config_t));
}

// ============================================================================
// Firmware Management
// ============================================================================

bool z1_bootloader_get_app_header(z1_firmware_header_t *header) {
    // Read header from application slot
    memcpy(header, (void*)APP_BASE, sizeof(z1_firmware_header_t));
    
    // Verify magic number
    if (header->magic != FIRMWARE_MAGIC) {
        return false;
    }
    
    // Verify version
    if (header->version != FIRMWARE_VERSION) {
        return false;
    }
    
    return true;
}

bool z1_bootloader_verify_firmware(void) {
    z1_firmware_header_t *header = (z1_firmware_header_t*)FIRMWARE_BUFFER_BASE;
    
    // Check magic number
    if (header->magic != FIRMWARE_MAGIC) {
        return false;
    }
    
    // Check size
    if (header->firmware_size > APP_MAX_SIZE) {
        return false;
    }
    
    // Verify CRC32
    uint8_t *firmware_code = (uint8_t*)(FIRMWARE_BUFFER_BASE + sizeof(z1_firmware_header_t));
    uint32_t calculated_crc = z1_bootloader_crc32(firmware_code, header->firmware_size);
    
    if (calculated_crc != header->crc32) {
        return false;
    }
    
    // Store header for later use
    memcpy(&g_firmware_buffer_header, header, sizeof(z1_firmware_header_t));
    
    return true;
}

bool z1_bootloader_install_firmware(void) {
    // Verify firmware first
    if (!z1_bootloader_verify_firmware()) {
        return false;
    }
    
    // Erase application slot
    // (Implementation would use RP2350 flash API)
    // flash_range_erase(APP_BASE - FLASH_BASE, APP_MAX_SIZE);
    
    // Copy firmware from buffer to application slot
    uint32_t total_size = sizeof(z1_firmware_header_t) + g_firmware_buffer_header.firmware_size;
    // flash_range_program(APP_BASE - FLASH_BASE,
    //                     (uint8_t*)FIRMWARE_BUFFER_BASE,
    //                     total_size);
    
    // Mark application as valid
    g_boot_config.app_valid = 1;
    g_boot_config.update_pending = 0;
    save_boot_config();
    
    return true;
}

void z1_bootloader_boot_application(void) {
    z1_firmware_header_t header;
    
    // Get application header
    if (!z1_bootloader_get_app_header(&header)) {
        // No valid application, stay in bootloader
        return;
    }
    
    // Verify CRC
    uint8_t *firmware_code = (uint8_t*)(APP_BASE + sizeof(z1_firmware_header_t));
    uint32_t calculated_crc = z1_bootloader_crc32(firmware_code, header.firmware_size);
    
    if (calculated_crc != header.crc32) {
        // Corrupted application, stay in bootloader
        g_boot_config.app_valid = 0;
        save_boot_config();
        return;
    }
    
    // Increment boot count
    g_boot_config.boot_count++;
    save_boot_config();
    
    // Calculate entry point
    uint32_t entry_point = APP_BASE + header.entry_point;
    
    // Disable interrupts
    __asm volatile ("cpsid i");
    
    // Set vector table offset
    // SCB->VTOR = APP_BASE;
    
    // Jump to application
    // (Implementation would set stack pointer and jump)
    void (*app_entry)(void) = (void (*)(void))entry_point;
    app_entry();
    
    // Should never reach here
    while(1);
}

// ============================================================================
// Z1 Bus Command Handlers
// ============================================================================

static void handle_firmware_info(void) {
    z1_firmware_info_t info;
    z1_firmware_header_t app_header;
    
    // Fill bootloader info
    info.bootloader_version = 0x00010000;  // v1.0.0
    info.boot_count = g_boot_config.boot_count;
    
    // Get application info
    if (z1_bootloader_get_app_header(&app_header)) {
        info.app_present = 1;
        memcpy(info.app_name, app_header.name, 32);
        memcpy(info.app_version, app_header.version_string, 16);
        info.app_size = app_header.firmware_size;
        info.app_crc32 = app_header.crc32;
    } else {
        info.app_present = 0;
        memset(info.app_name, 0, 32);
        memset(info.app_version, 0, 16);
        info.app_size = 0;
        info.app_crc32 = 0;
    }
    
    // Send response over Z1 bus
    // z1_bus_send(&info, sizeof(z1_firmware_info_t));
}

static void handle_firmware_upload(uint32_t offset, const uint8_t *data, uint16_t len) {
    // Write data to firmware buffer
    if (offset + len <= FIRMWARE_BUFFER_SIZE) {
        memcpy((void*)(FIRMWARE_BUFFER_BASE + offset), data, len);
        g_firmware_upload_offset = offset + len;
        
        // Send ACK
        // z1_bus_send_ack();
    } else {
        // Send NACK (buffer overflow)
        // z1_bus_send_nack();
    }
}

static void handle_firmware_verify(void) {
    bool valid = z1_bootloader_verify_firmware();
    
    // Send response
    uint8_t response = valid ? 1 : 0;
    // z1_bus_send(&response, 1);
}

static void handle_firmware_install(void) {
    bool success = z1_bootloader_install_firmware();
    
    // Send response
    uint8_t response = success ? 1 : 0;
    // z1_bus_send(&response, 1);
}

static void handle_firmware_activate(void) {
    // Set boot mode to application
    g_boot_config.boot_mode = BOOT_MODE_APPLICATION;
    save_boot_config();
    
    // Send ACK
    // z1_bus_send_ack();
    
    // Reboot
    // system_reset();
}

static void handle_boot_mode(uint32_t mode) {
    g_boot_config.boot_mode = mode;
    save_boot_config();
    
    // Send ACK
    // z1_bus_send_ack();
}

static void handle_reboot(void) {
    // Send ACK
    // z1_bus_send_ack();
    
    // Small delay to ensure ACK is sent
    // delay_ms(10);
    
    // Reboot
    // system_reset();
}

// ============================================================================
// Main Bootloader Functions
// ============================================================================

void z1_bootloader_init(void) {
    // Initialize hardware
    // - GPIO
    // - Z1 bus interface
    // - Flash controller
    // - PSRAM
    
    // Load boot configuration
    load_boot_config();
    
    // Reset firmware upload state
    g_firmware_upload_offset = 0;
}

void z1_bootloader_main(void) {
    // Check boot mode
    if (g_boot_config.boot_mode == BOOT_MODE_APPLICATION && g_boot_config.app_valid) {
        // Try to boot application
        z1_bootloader_boot_application();
        // If we get here, application boot failed
    }
    
    // Stay in bootloader mode
    // Main loop: handle Z1 bus commands
    while (1) {
        // Check for incoming Z1 bus message
        // uint8_t cmd;
        // if (z1_bus_receive_command(&cmd)) {
        //     switch (cmd) {
        //         case Z1_CMD_FIRMWARE_INFO:
        //             handle_firmware_info();
        //             break;
        //         case Z1_CMD_FIRMWARE_UPLOAD:
        //             // Read offset, length, data
        //             handle_firmware_upload(offset, data, len);
        //             break;
        //         case Z1_CMD_FIRMWARE_VERIFY:
        //             handle_firmware_verify();
        //             break;
        //         case Z1_CMD_FIRMWARE_INSTALL:
        //             handle_firmware_install();
        //             break;
        //         case Z1_CMD_FIRMWARE_ACTIVATE:
        //             handle_firmware_activate();
        //             break;
        //         case Z1_CMD_BOOT_MODE:
        //             handle_boot_mode(mode);
        //             break;
        //         case Z1_CMD_REBOOT:
        //             handle_reboot();
        //             break;
        //     }
        // }
        
        // Blink LED to indicate bootloader mode
        // toggle_led();
        // delay_ms(500);
    }
}

// ============================================================================
// Bootloader API Table
// ============================================================================

// This table is placed at a fixed address (BOOTLOADER_API_ADDR)
// and provides services to application firmware
__attribute__((section(".bootloader_api")))
const z1_bootloader_api_t bootloader_api = {
    .version = 0x00010000,  // v1.0.0
    
    // Z1 Bus functions (would be implemented)
    .bus_send = NULL,  // z1_bus_send_impl
    .bus_receive = NULL,  // z1_bus_receive_impl
    
    // Flash functions
    .flash_erase = NULL,  // flash_erase_impl
    .flash_write = NULL,  // flash_write_impl
    .flash_read = NULL,   // flash_read_impl
    
    // PSRAM functions
    .psram_write = NULL,  // psram_write_impl
    .psram_read = NULL,   // psram_read_impl
    
    // System functions
    .system_reset = NULL,  // system_reset_impl
    .get_system_time_ms = NULL,  // get_system_time_ms_impl
    
    // Utility functions
    .crc32 = z1_bootloader_crc32,
};

// ============================================================================
// Entry Point
// ============================================================================

int main(void) {
    z1_bootloader_init();
    z1_bootloader_main();
    
    // Should never reach here
    while(1);
    return 0;
}
