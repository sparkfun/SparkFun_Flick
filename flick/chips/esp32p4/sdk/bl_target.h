/*
 * bl_target.h - ESP32-P4 chip, shared by every ESP32-P4 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32-P4"
#define BL_TARGET_CHIP_ID       18          /* == esp_chip_id_t ESP_CHIP_ID_ESP32P4 */
#define BL_TARGET_BOOT_PIN      35          /* the BOOT button (a strapping pin) (from Arduino's esp32p4 variant; not yet checked on the board) */

/* ---- Flash window ------------------------------------------------------
 * The P4 maps flash for instructions and data at the same addresses
 * (SOC_MMU_DI_VADDR_SHARED): 64 MB from 0x40000000, 1024 entries of 64 KB
 * (PSRAM has its own MMU and window at 0x48000000). Baseline's code and
 * rodata are mapped from the bottom; the sketch window sits 56 MB in, so
 * IBUS == DBUS. */
#define BL_SKETCH_IBUS_BASE     0x43800000
#define BL_SKETCH_DBUS_BASE     0x43800000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Just below the
 * end of the HP L2 memory region (SRAM_END 0x4FFAEFC0 in IDF's P4 memory.ld,
 * where the bootloader's loader code sits). CHECK _heap_start after each
 * Baseline build. */
#define BL_SKETCH_RAM_BASE      0x4FFA6000
#define BL_SKETCH_RAM_SIZE      0x8000      /* 32 KB */

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_ESP32     1       /* Arduino-ESP32 / ESP-IDF names (esp_*, ESP, ledc*, ps_malloc, ...) */

#define BL_TARGET_CHIP          "esp32p4"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
