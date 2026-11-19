/*
 * bl_target.h - Ambiq Apollo3 Blue (SparkFun Artemis module), shared by every
 * Artemis product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 *
 * Flash (1 MB, 8 KB pages, code runs from it in place at address 0):
 * 0x000000  Ambiq secure bootloader (48 KB, in the chip)
 * 0x00C000  SparkFun Variable Loader (SVL, 16 KB): kept, flashes Baseline over serial
 * 0x010000  Baseline, up to 448 KB
 * 0x080000  LittleFS, 112 KB
 * 0x09C000  the last crash's text: one page, written when a crash happens
 * 0x09E000  Baseline's state (active slot, trial): one page
 * 0x0A0000  the sketch window: a copy of the active slot, 128 KB
 * 0x0C0000  sketch slot A, 128 KB
 * 0x0E0000  sketch slot B, 128 KB
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "Apollo3"
#define BL_TARGET_CHIP_ID       0xA003      /* not an esp_chip_id_t: Espressif's IDs are small numbers */
#define BL_TARGET_BOOT_PIN      (-1)        /* no BOOT button: an Artemis has only reset (SVL catches uploads) */

/* ---- Flash window ------------------------------------------------------
 * The Cortex-M4F runs code straight from internal flash. The Apollo3 can't
 * remap flash, so the window is a fixed flash region the size of a slot:
 * Baseline copies the active slot into it whenever another slot becomes
 * active (after an upload, or a rollback), as on the RP2040. Sketches are
 * linked once, at the window; the two slots keep the old sketch for a
 * rollback. Code and data share it. */
#define BL_SKETCH_IBUS_BASE     0x000A0000
#define BL_SKETCH_DBUS_BASE     0x000A0000
#define BL_SKETCH_MAX_SIZE      0x20000     /* 128 KB: a slot */
#define BL_TARGET_COPIED_WINDOW 1           /* the window is a copy, not a mapping */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss: the top 64 KB of the 384 KB SRAM. SVL's stack, at the
 * top of SRAM, runs over it at every reset; it needn't survive one. */
#define BL_SKETCH_RAM_BASE      0x10050000
#define BL_SKETCH_RAM_SIZE      0x10000     /* 64 KB */

/* ---- Baseline only ----------------------------------------------------- */
#define BL_APOLLO3_KEEP_BASE    0x1004F000  /* the heap ends here (4 KB spare below the sketch RAM window) */
#define BL_APOLLO3_BASELINE_MAX 0x70000     /* Baseline at 0x10000, up to 448 KB */
#define BL_APOLLO3_FS_START     0x00080000
#define BL_APOLLO3_FS_END       0x0009C000
#define BL_APOLLO3_CRASH_PAGE   0x0009C000
#define BL_APOLLO3_STATE_PAGE   0x0009E000
#define BL_APOLLO3_SLOT_A       0x000C0000
#define BL_APOLLO3_SLOT_B       0x000E0000
#define BL_APOLLO3_SLOT_SIZE    0x20000
#define BL_APOLLO3_FLASH_SIZE   0x100000

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_APOLLO3   1
#define BL_TARGET_ADC_MV      2000    /* the ADC's full scale: 0-2.0 V (14-bit, read as 0-4095) */
#define BL_HAS_BLE            1       /* Bluetooth LE (BLESerial, BLEDevice, ...): ArduinoBLE on the chip's own radio */

#define BL_TARGET_CHIP          "apollo3"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
