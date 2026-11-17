/*
 * bl_target.h - Raspberry Pi RP2040 chip, shared by every RP2040 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins,
 * where the sketch slots sit in flash) are in the product target's bl_board.h,
 * included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "RP2040"
#define BL_TARGET_CHIP_ID       0x2040      /* not an esp_chip_id_t: Espressif's IDs are small numbers */
#define BL_TARGET_BOOT_PIN      (-1)        /* none: BOOTSEL is the flash chip select, not a GPIO */

/* ---- Flash window ------------------------------------------------------
 * The Cortex-M0+ runs code straight from QSPI flash through the XIP cache, from
 * 0x10000000. The RP2040 can't remap flash, so the window is a fixed flash
 * region the size of a slot, at the same address as the RP2350's: Baseline
 * copies the active slot into it whenever another slot becomes active (after
 * an upload, or a rollback). Sketches are linked once, at the window, and the
 * two slots keep the old sketch for a rollback. Code and data share it. */
#define BL_SKETCH_IBUS_BASE     0x10C00000
#define BL_SKETCH_DBUS_BASE     0x10C00000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB: a slot */
#define BL_TARGET_COPIED_WINDOW 1           /* the window is a copy, not a mapping */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, near the top of the 256 KB of striped SRAM. Baseline's
 * heap ends below it (and below BL_RP2_KEEP_BASE); above it a 12 KB gap, then
 * the 8 KB scratch banks with core 0's main stack. */
#define BL_SKETCH_RAM_BASE      0x20035000
#define BL_SKETCH_RAM_SIZE      0x8000      /* 32 KB */

/* ---- Baseline only: 4 KB below the window survive a reset (the sketch's RTC
 * memory, Baseline's state). arduino-pico's OTA boot stub, which runs before
 * every start, uses the first 12 KB of RAM and 0x20035000-0x20040000 itself
 * (the window's contents needn't survive), so this can't be the linker's
 * .uninitialized_data. The heap ends below it (flick sets arduino-pico's RAM
 * length from it). */
#define BL_RP2_KEEP_BASE        0x20034000

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_RP2       1       /* arduino-pico names (later: rp2040.*, PIO) */

#define BL_TARGET_CHIP          "rp2040"      /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
