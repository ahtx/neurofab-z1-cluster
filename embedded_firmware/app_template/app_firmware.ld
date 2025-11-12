/**
 * Linker Script for Z1 Application Firmware
 * 
 * This linker script is used to build application firmware that runs
 * on top of the Z1 bootloader.
 * 
 * Memory Map:
 *   Flash: 0x10004000 - 0x1001FFFF (112KB application slot)
 *   SRAM:  0x20000000 - 0x20080000 (512KB)
 *   PSRAM: 0x20100000 - 0x207FFFFF (7MB for application use)
 */

MEMORY
{
    /* Application firmware slot in flash */
    FLASH (rx) : ORIGIN = 0x10004000, LENGTH = 112K
    
    /* SRAM for code and data */
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
    
    /* PSRAM for large data structures */
    PSRAM (rw) : ORIGIN = 0x20100000, LENGTH = 7M
}

/* Entry point */
ENTRY(app_entry)

SECTIONS
{
    /* Firmware header (256 bytes) */
    .firmware_header : ALIGN(4)
    {
        KEEP(*(.firmware_header))
        . = ALIGN(256);
    } > FLASH
    
    /* Vector table */
    .app_vectors : ALIGN(256)
    {
        KEEP(*(.app_vectors))
    } > FLASH
    
    /* Code */
    .text : ALIGN(4)
    {
        *(.app_entry)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    
    /* ARM exception unwinding */
    .ARM.extab : ALIGN(4)
    {
        *(.ARM.extab*)
    } > FLASH
    
    .ARM.exidx : ALIGN(4)
    {
        *(.ARM.exidx*)
    } > FLASH
    
    /* Initialized data (copied to SRAM at startup) */
    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > SRAM AT > FLASH
    
    __data_load__ = LOADADDR(.data);
    
    /* Uninitialized data */
    .bss : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > SRAM
    
    /* Heap (grows upward) */
    .heap : ALIGN(4)
    {
        __heap_start__ = .;
        . = . + 64K;  /* 64KB heap */
        __heap_end__ = .;
    } > SRAM
    
    /* Stack (grows downward from end of SRAM) */
    __stack_top__ = ORIGIN(SRAM) + LENGTH(SRAM);
    
    /* PSRAM sections for large data */
    .psram_data (NOLOAD) : ALIGN(4)
    {
        *(.psram_data*)
        . = ALIGN(4);
    } > PSRAM
    
    /* Discard debug info to save space */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
