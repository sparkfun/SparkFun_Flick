/*
 * bl_target.h - ESP32-C5 chip, shared by every ESP32-C5 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 * NOT YET RUN ON HARDWARE.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32-C5"
#define BL_TARGET_CHIP_ID       23          /* == esp_chip_id_t ESP_CHIP_ID_ESP32C5 */
#define BL_TARGET_BOOT_PIN      28          /* the BOOT button (a strapping pin) (from the datasheet; not yet checked on the board) */

/* ---- Flash window ------------------------------------------------------
 * The C5 maps flash and PSRAM for instructions and data at the same
 * addresses (SOC_MMU_DI_VADDR_SHARED): 32 MB from 0x42000000, 512 entries of
 * 64 KB. Baseline's code, rodata and up to 8 MB of PSRAM are mapped from the
 * bottom; the sketch window sits 24 MB in (as on the S3), so IBUS == DBUS. */
#define BL_SKETCH_IBUS_BASE     0x43800000
#define BL_SKETCH_DBUS_BASE     0x43800000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Just below the
 * end of the static-data region (SRAM_SEG_END 0x4084E5A0 in IDF memory.ld).
 * CHECK _heap_start after each Baseline build. */
#define BL_SKETCH_RAM_BASE      0x40846000
#define BL_SKETCH_RAM_SIZE      0x8000      /* 32 KB */

#define BL_TARGET_CHIP          "esp32c5"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
