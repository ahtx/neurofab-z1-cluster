/**
 * Z1 Bootloader
 * 
 * Generic bootloader for NeuroFab Z1 compute nodes.
 * Provides firmware update capability via Z1 bus.
 * 
 * Copyright NeuroFab Corp. All rights reserved.
 */

#ifndef Z1_BOOTLOADER_H
#define Z1_BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Memory Map
// ============================================================================

// Flash memory layout (RP2350B - 2MB flash)
#define FLASH_BASE              0x10000000
#define BOOTLOADER_BASE         0x10000000  // 16KB bootloader
#define BOOTLOADER_SIZE         0x00004000  // 16KB
#define APP_BASE                0x10004000  // Application firmware slot
#define APP_MAX_SIZE            0x0001C000  // 112KB max application size
#define FIRMWARE_BUFFER_BASE    0x10020000  // Firmware update buffer
#define FIRMWARE_BUFFER_SIZE    0x00020000  // 128KB buffer
#define USER_DATA_BASE          0x10040000  // User data / filesystem

// PSRAM memory layout (8MB)
#define PSRAM_BASE              0x20000000
#define BOOTLOADER_RAM_BASE     0x20000000  // 1MB for bootloader
#define BOOTLOADER_RAM_SIZE     0x00100000
#define APP_DATA_BASE           0x20100000  // 5MB for application data
#define APP_DATA_SIZE           0x00500000
#define FIRMWARE_EXEC_BASE      0x20600000  // 2MB for execute-from-RAM (optional)
#define FIRMWARE_EXEC_SIZE      0x00200000

// ============================================================================
// Firmware Header
// ============================================================================

#define FIRMWARE_MAGIC          0x4E465A31  // "NFZ1" in hex
#define FIRMWARE_VERSION        1

/**
 * Firmware header (256 bytes)
 * Placed at the start of every application firmware image
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;              // Magic number (0x4E465A31)
    uint32_t version;            // Header version
    uint32_t firmware_size;      // Size of firmware code (bytes)
    uint32_t entry_point;        // Entry point offset from APP_BASE
    uint32_t crc32;              // CRC32 of firmware code
    uint32_t timestamp;          // Build timestamp (Unix time)
    char     name[32];           // Firmware name (null-terminated)
    char     version_string[16]; // Version string (e.g., "1.0.0")
    uint32_t capabilities;       // Capability flags
    uint32_t reserved[42];       // Reserved for future use
} z1_firmware_header_t;

// Firmware capability flags
#define FW_CAP_SNN              0x00000001  // Spiking neural network
#define FW_CAP_MATRIX           0x00000002  // Matrix operations
#define FW_CAP_SIGNAL           0x00000004  // Signal processing
#define FW_CAP_CUSTOM           0x80000000  // Custom/user-defined

// ============================================================================
// Boot Configuration
// ============================================================================

#define BOOT_FLAG_ADDR          (USER_DATA_BASE)
#define BOOT_FLAG_MAGIC         0x424F4F54  // "BOOT"

/**
 * Boot configuration stored in flash
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;              // Magic number (0x424F4F54)
    uint32_t boot_mode;          // Boot mode selector
    uint32_t app_valid;          // Application firmware valid flag
    uint32_t update_pending;     // Firmware update pending flag
    uint32_t boot_count;         // Number of boots
    uint32_t last_boot_status;   // Last boot status code
    uint32_t reserved[10];       // Reserved
} z1_boot_config_t;

// Boot modes
#define BOOT_MODE_APPLICATION   0  // Boot to application firmware
#define BOOT_MODE_BOOTLOADER    1  // Stay in bootloader
#define BOOT_MODE_RECOVERY      2  // Recovery mode
#define BOOT_MODE_UPDATE        3  // Firmware update mode

// ============================================================================
// Bootloader API
// ============================================================================

/**
 * Bootloader API table
 * Provides services to application firmware
 */
typedef struct {
    uint32_t version;
    
    // Z1 Bus functions
    void (*bus_send)(uint8_t node_id, const void *data, uint16_t len);
    bool (*bus_receive)(void *data, uint16_t max_len, uint16_t *actual_len);
    
    // Flash functions
    bool (*flash_erase)(uint32_t addr, uint32_t len);
    bool (*flash_write)(uint32_t addr, const void *data, uint32_t len);
    bool (*flash_read)(uint32_t addr, void *data, uint32_t len);
    
    // PSRAM functions
    bool (*psram_write)(uint32_t addr, const void *data, uint32_t len);
    bool (*psram_read)(uint32_t addr, void *data, uint32_t len);
    
    // System functions
    void (*system_reset)(void);
    uint32_t (*get_system_time_ms)(void);
    
    // Utility functions
    uint32_t (*crc32)(const void *data, uint32_t len);
    
} z1_bootloader_api_t;

// Bootloader API is located at fixed address
#define BOOTLOADER_API_ADDR     (BOOTLOADER_BASE + 0x100)
#define GET_BOOTLOADER_API()    ((z1_bootloader_api_t*)(BOOTLOADER_API_ADDR))

// ============================================================================
// Z1 Bus Protocol Extensions for Bootloader
// ============================================================================

// Bootloader-specific commands (0x30-0x3F)
#define Z1_CMD_FIRMWARE_INFO        0x30  // Get firmware information
#define Z1_CMD_FIRMWARE_UPLOAD      0x31  // Upload firmware to buffer
#define Z1_CMD_FIRMWARE_VERIFY      0x32  // Verify firmware in buffer
#define Z1_CMD_FIRMWARE_INSTALL     0x33  // Install firmware from buffer
#define Z1_CMD_FIRMWARE_ACTIVATE    0x34  // Activate new firmware and reboot
#define Z1_CMD_BOOT_MODE            0x35  // Set boot mode
#define Z1_CMD_REBOOT               0x36  // Reboot node

/**
 * Firmware info response
 */
typedef struct __attribute__((packed)) {
    uint32_t bootloader_version;
    uint32_t app_present;
    char     app_name[32];
    char     app_version[16];
    uint32_t app_size;
    uint32_t app_crc32;
    uint32_t boot_count;
} z1_firmware_info_t;

// ============================================================================
// Function Prototypes
// ============================================================================

/**
 * Initialize bootloader
 */
void z1_bootloader_init(void);

/**
 * Main bootloader loop
 * Handles firmware updates and boot selection
 */
void z1_bootloader_main(void);

/**
 * Verify firmware in buffer
 * 
 * @return true if firmware is valid
 */
bool z1_bootloader_verify_firmware(void);

/**
 * Install firmware from buffer to application slot
 * 
 * @return true if successful
 */
bool z1_bootloader_install_firmware(void);

/**
 * Boot to application firmware
 * 
 * @return Does not return if successful
 */
void z1_bootloader_boot_application(void);

/**
 * Calculate CRC32
 * 
 * @param data Data buffer
 * @param len Length in bytes
 * @return CRC32 value
 */
uint32_t z1_bootloader_crc32(const void *data, uint32_t len);

/**
 * Get firmware header from application slot
 * 
 * @param header Pointer to header structure to fill
 * @return true if valid header found
 */
bool z1_bootloader_get_app_header(z1_firmware_header_t *header);

#endif // Z1_BOOTLOADER_H
