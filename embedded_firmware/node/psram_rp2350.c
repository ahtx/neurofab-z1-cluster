#include "psram_rp2350.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/xip.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include <stdio.h>

// Private state variables
static bool psram_initialized = false;
static bool quad_mode_success = false;
static size_t psram_size = 0;
static uint32_t psram_base_address = PSRAM_BASE_ADDRESS;

// Private function declarations
static size_t __no_inline_not_in_flash_func(detect_psram_size)(void);
static bool __no_inline_not_in_flash_func(setup_psram_hardware)(void);

// Public API implementation

bool psram_init(void) {
    printf("\n[PSRAM] PSRAM Setup Starting...\n");
    printf("Target: APS6404L 8MB QSPI PSRAM (RP2350)\n");
    
    // Set PSRAM CS pin function
    gpio_set_function(PSRAM_CS_PIN, GPIO_FUNC_XIP_CS1);
    printf("✅ PSRAM CS pin set to XIP_CS1 function\n");
    
    // Get PSRAM size
    psram_size = detect_psram_size();
    if (psram_size == 0) {
        printf("❌ PSRAM not detected or size detection failed\n");
        return false;
    }
    
    printf("⚡ PSRAM ID detected - proceeding to configuration\n");
    
    // Setup hardware configuration
    if (!setup_psram_hardware()) {
        printf("❌ PSRAM hardware setup failed\n");
        return false;
    }
    
    psram_initialized = true;
    printf("🎉 PSRAM Setup Complete!\n");
    printf("   Size: %zu MB\n", psram_size / (1024*1024));
    printf("   Base: 0x%08lX\n", (unsigned long)psram_base_address);
    printf("   Mode: %s operations\n", quad_mode_success ? "QUAD-line high-speed" : "Single-line conservative");
    
    return true;
}

size_t psram_get_size(void) {
    return psram_initialized ? psram_size : 0;
}

uint32_t psram_get_base_address(void) {
    return psram_base_address;
}

bool psram_is_initialized(void) {
    return psram_initialized;
}

bool psram_is_quad_mode(void) {
    return psram_initialized && quad_mode_success;
}

bool psram_test_memory(uint32_t test_address) {
    if (!psram_initialized) {
        return false;
    }
    
    // Ensure address is within PSRAM range
    if (test_address < psram_base_address || 
        test_address >= (psram_base_address + psram_size)) {
        return false;
    }
    
    volatile uint8_t *test_ptr = (volatile uint8_t*)test_address;
    uint8_t original = *test_ptr;
    
    __asm volatile("dsb sy; isb" ::: "memory");
    *test_ptr = 0xA5;
    __asm volatile("dsb sy; isb" ::: "memory");
    sleep_us(10);
    
    uint8_t read_back = *test_ptr;
    
    *test_ptr = original;
    __asm volatile("dsb sy; isb" ::: "memory");
    
    return (read_back == 0xA5);
}

volatile uint8_t* psram_get_pointer(size_t offset) {
    if (!psram_initialized || offset >= psram_size) {
        return NULL;
    }
    
    return (volatile uint8_t*)(psram_base_address + offset);
}

// Private implementation functions

static size_t __no_inline_not_in_flash_func(detect_psram_size)(void) {
    size_t detected_size = 0;
    
    printf("[DETECT] Starting PSRAM ID detection...\n");
    
    // Direct CSR communication for PSRAM ID detection
    qmi_hw->direct_csr = 3 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    
    // Wait for cooldown
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
    
    // Exit QPI quad mode first
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | PSRAM_CMD_QUAD_END;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    
    // Read PSRAM ID
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;
    
    for (size_t i = 0; i < 7; i++) {
        qmi_hw->direct_tx = (i == 0 ? PSRAM_CMD_READ_ID : PSRAM_CMD_NOOP);
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {}
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
        
        if (i == 5) kgd = qmi_hw->direct_rx;
        if (i == 6) eid = qmi_hw->direct_rx; 
        else (void)qmi_hw->direct_rx;
    }
    
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_EN_BITS);
    
    // Check if this is the correct PSRAM
    if (kgd == PSRAM_ID) {
        detected_size = 1024 * 1024; // 1 MiB base
        uint8_t size_id = eid >> 5;
        if (eid == 0x26 || size_id == 2) {
            detected_size *= 8; // 8MB
        } else if (size_id == 0) {
            detected_size *= 2; // 2MB
        } else if (size_id == 1) {
            detected_size *= 4; // 4MB
        }
        printf("✅ PSRAM ID: 0x%02X, EID: 0x%02X, Size: %zu MB\n", kgd, eid, detected_size/(1024*1024));
    } else {
        printf("❌ PSRAM ID mismatch: 0x%02X (expected 0x%02X)\n", kgd, PSRAM_ID);
    }
    
    return detected_size;
}

static bool __no_inline_not_in_flash_func(setup_psram_hardware)(void) {
    // Configure M1 for SERIAL SPI mode first
    printf("[SETUP] Configuring M1 for SERIAL SPI setup mode...\n");
    
    uint32_t sys_freq = clock_get_hz(clk_sys);
    uint32_t setup_clk_div = (sys_freq + 75000000 - 1) / 75000000;
    
    // Serial SPI Read format - Fast Read (0x0B) with dummy cycles
    qmi_hw->m[1].rfmt = (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_S << QMI_M1_RFMT_PREFIX_WIDTH_LSB |
                         QMI_M1_RFMT_ADDR_WIDTH_VALUE_S << QMI_M1_RFMT_ADDR_WIDTH_LSB |
                         QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_S << QMI_M1_RFMT_SUFFIX_WIDTH_LSB |
                         QMI_M1_RFMT_DUMMY_WIDTH_VALUE_S << QMI_M1_RFMT_DUMMY_WIDTH_LSB |
                         QMI_M1_RFMT_DUMMY_LEN_VALUE_4 << QMI_M1_RFMT_DUMMY_LEN_LSB |
                         QMI_M1_RFMT_DATA_WIDTH_VALUE_S << QMI_M1_RFMT_DATA_WIDTH_LSB |
                         QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB |
                         0 << QMI_M1_RFMT_SUFFIX_LEN_LSB);

    qmi_hw->m[1].rcmd = 0x0B << QMI_M1_RCMD_PREFIX_LSB | 0 << QMI_M1_RCMD_SUFFIX_LSB;

    // Serial SPI Write format - Page Program (0x02)
    qmi_hw->m[1].wfmt = (QMI_M1_WFMT_PREFIX_WIDTH_VALUE_S << QMI_M1_WFMT_PREFIX_WIDTH_LSB |
                         QMI_M1_WFMT_ADDR_WIDTH_VALUE_S << QMI_M1_WFMT_ADDR_WIDTH_LSB |
                         QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_S << QMI_M1_WFMT_SUFFIX_WIDTH_LSB |
                         0 << QMI_M1_WFMT_DUMMY_WIDTH_LSB |
                         0 << QMI_M1_WFMT_DUMMY_LEN_LSB |
                         QMI_M1_WFMT_DATA_WIDTH_VALUE_S << QMI_M1_WFMT_DATA_WIDTH_LSB |
                         QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB |
                         0 << QMI_M1_WFMT_SUFFIX_LEN_LSB);

    qmi_hw->m[1].wcmd = 0x02 << QMI_M1_WCMD_PREFIX_LSB | 0 << QMI_M1_WCMD_SUFFIX_LSB;

    // Conservative timing for setup
    qmi_hw->m[1].timing = (2 << QMI_M1_TIMING_COOLDOWN_LSB) |
                          (2 << QMI_M1_TIMING_RXDELAY_LSB) |
                          (2 << QMI_M1_TIMING_SELECT_SETUP_LSB) |
                          (2 << QMI_M1_TIMING_SELECT_HOLD_LSB) |
                          (setup_clk_div << QMI_M1_TIMING_CLKDIV_LSB);
    
    printf("   ✅ Serial SPI M1 configured: clock=%u MHz\n", sys_freq / setup_clk_div / 1000000);

    // Send Enter Quad Mode using serial SPI
    printf("🚀 Sending Enter Quad Mode (0x35) via serial SPI...\n");
    
    qmi_hw->direct_csr = setup_clk_div << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
    
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_tx = PSRAM_CMD_QUAD_ENABLE;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_EN_BITS);
    sleep_us(200);
    printf("   ✅ Enter Quad Mode sent\n");

    // Configure M1 for QUAD operations
    printf("[SETUP] Configuring M1 for QUAD operations...\n");
    
    quad_mode_success = false;

    // QUAD Read format - Fast Read Quad (0xEB)
    qmi_hw->m[1].rfmt = (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB |
                         QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB |
                         QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB |
                         QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB |
                         6 << QMI_M1_RFMT_DUMMY_LEN_LSB |
                         QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB |
                         QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB |
                         0 << QMI_M1_RFMT_SUFFIX_LEN_LSB);

    qmi_hw->m[1].rcmd = PSRAM_CMD_QUAD_READ << QMI_M1_RCMD_PREFIX_LSB | 0 << QMI_M1_RCMD_SUFFIX_LSB;

    // QUAD Write format - Quad Write (0x38)
    qmi_hw->m[1].wfmt = (QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB |
                         QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_WFMT_ADDR_WIDTH_LSB |
                         QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB |
                         0 << QMI_M1_WFMT_DUMMY_WIDTH_LSB |
                         0 << QMI_M1_WFMT_DUMMY_LEN_LSB |
                         QMI_M1_WFMT_DATA_WIDTH_VALUE_Q << QMI_M1_WFMT_DATA_WIDTH_LSB |
                         QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB |
                         0 << QMI_M1_WFMT_SUFFIX_LEN_LSB);

    qmi_hw->m[1].wcmd = PSRAM_CMD_QUAD_WRITE << QMI_M1_WCMD_PREFIX_LSB | 0 << QMI_M1_WCMD_SUFFIX_LSB;

    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;

    uint32_t quad_clk_div = (sys_freq + 75000000 - 1) / 75000000;
    
    qmi_hw->m[1].timing = (3 << QMI_M1_TIMING_COOLDOWN_LSB) |
                          (2 << QMI_M1_TIMING_RXDELAY_LSB) |
                          (2 << QMI_M1_TIMING_SELECT_SETUP_LSB) |
                          (3 << QMI_M1_TIMING_SELECT_HOLD_LSB) |
                          (quad_clk_div << QMI_M1_TIMING_CLKDIV_LSB);

    printf("   ✅ QUAD M1 configured: clock=%u MHz\n", sys_freq / quad_clk_div / 1000000);

    // Test QUAD mode connectivity
    printf("🔍 Testing QUAD mode connectivity...\n");
    
    volatile uint8_t *test_ptr = (volatile uint8_t*)psram_base_address;
    uint8_t original = *test_ptr;
    
    __asm volatile("dsb sy; isb" ::: "memory");
    *test_ptr = 0x42;
    __asm volatile("dsb sy; isb" ::: "memory");
    sleep_us(10);
    
    uint8_t read_back = *test_ptr;
    
    if (read_back == 0x42) {
        quad_mode_success = true;
        printf("   ✅ QUAD mode test passed!\n");
    } else {
        quad_mode_success = false;
        printf("   ❌ QUAD mode test failed\n");
    }
    
    *test_ptr = original;
    __asm volatile("dsb sy; isb" ::: "memory");

    // Quick 1KB test if QUAD mode working
    if (quad_mode_success) {
        printf("[TEST] Quick 1KB QUAD mode test...\n");
        for (int i = 0; i < 1024; i++) {
            volatile uint8_t *ptr = (volatile uint8_t*)(psram_base_address + i);
            __asm volatile("dsb sy; isb" ::: "memory");
            *ptr = (uint8_t)(i & 0xFF);
            __asm volatile("dsb sy; isb" ::: "memory");
            sleep_us(1);
            
            uint8_t readback = *ptr;
            if (readback != (uint8_t)(i & 0xFF)) {
                printf("   ❌ QUAD test failed at byte %d\n", i);
                quad_mode_success = false;
                break;
            }
        }
        if (quad_mode_success) {
            printf("   ✅ Quick 1KB QUAD test passed!\n");
        }
    }

    printf("✅ QMI M1 configured for %s operations\n", quad_mode_success ? "QUAD" : "SINGLE-LINE");
    
    // Test basic connectivity
    printf("🔗 Testing PSRAM connectivity...\n");
    
    bool write_success = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        volatile uint8_t *test_ptr = (volatile uint8_t*)psram_base_address;
        uint8_t original = *test_ptr;
        
        __asm volatile("dsb sy; isb" ::: "memory");
        *test_ptr = 0xA5;
        __asm volatile("dsb sy; isb" ::: "memory");
        sleep_us(100 * attempt);
        
        uint8_t read_back = *test_ptr;
        
        if (read_back == 0xA5) {
            write_success = true;
            *test_ptr = original;
            break;
        } else {
            *test_ptr = original;
        }
    }
    
    if (!write_success) {
        printf("❌ PSRAM connectivity test failed!\n");
        return false;
    }
    
    printf("✅ PSRAM connectivity confirmed\n");
    return true;
}

// Wrapper functions for compatibility with z1_snn_engine.c

/**
 * @brief Read data from PSRAM
 * 
 * @param address PSRAM address to read from
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return true if read successful, false otherwise
 */
bool psram_read(uint32_t address, void* buffer, size_t length) {
    if (!psram_initialized || !buffer || length == 0) {
        return false;
    }
    
    // Check if address is within PSRAM range
    if (address < psram_base_address || 
        (address + length) > (psram_base_address + psram_size)) {
        return false;
    }
    
    // Calculate offset from base address
    size_t offset = address - psram_base_address;
    
    // Get pointer to PSRAM
    volatile uint8_t* psram_ptr = psram_get_pointer(offset);
    if (!psram_ptr) {
        return false;
    }
    
    // Copy data from PSRAM to buffer
    memcpy(buffer, (const void*)psram_ptr, length);
    
    return true;
}

/**
 * @brief Write data to PSRAM
 * 
 * @param address PSRAM address to write to
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return true if write successful, false otherwise
 */
bool psram_write(uint32_t address, const void* buffer, size_t length) {
    if (!psram_initialized || !buffer || length == 0) {
        return false;
    }
    
    // Check if address is within PSRAM range
    if (address < psram_base_address || 
        (address + length) > (psram_base_address + psram_size)) {
        return false;
    }
    
    // Calculate offset from base address
    size_t offset = address - psram_base_address;
    
    // Get pointer to PSRAM
    volatile uint8_t* psram_ptr = psram_get_pointer(offset);
    if (!psram_ptr) {
        return false;
    }
    
    // Copy data from buffer to PSRAM
    memcpy((void*)psram_ptr, buffer, length);
    
    // Memory barrier to ensure write completes
    __asm volatile("dsb sy; isb" ::: "memory");
    
    return true;
}
