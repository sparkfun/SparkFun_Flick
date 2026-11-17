/*
 * bl_target.h - Raspberry Pi RP2350 chip (Arm cores), shared by every RP2350
 * product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins,
 * where the sketch slots sit in flash) are in the product target's bl_board.h,
 * included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "RP2350"
#define BL_TARGET_CHIP_ID       0x2350      /* not an esp_chip_id_t: Espressif's IDs are small numbers */
#define BL_TARGET_BOOT_PIN      (-1)        /* none: BOOTSEL is the flash chip select, not a GPIO */

/* ---- Flash window ------------------------------------------------------
 * The Cortex-M33 runs code straight from QSPI flash through the XIP cache, from
 * 0x10000000. The QMI translates each 4 MB quarter of that 16 MB window on its
 * own (ATRANS0-3). Baseline points the last quarter at the active sketch slot
 * (ATRANS3), so sketches are linked once, at the window, whichever slot they
 * land in. Code and data share it. Flash under that quarter is reached only
 * through it, so the boards keep their sketch slots there. */
#define BL_SKETCH_IBUS_BASE     0x10C00000
#define BL_SKETCH_DBUS_BASE     0x10C00000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB: a slot */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, at the top of main SRAM. Baseline's heap ends below it
 * (and below BL_RP2_KEEP_BASE); above it an 8 KB gap, then the
 * 8 KB scratch banks with core 0's main stack. Baseline sets MSPLIM at the
 * window's end, so that stack faults instead of running into the window. */
#define BL_SKETCH_RAM_BASE      0x2006E000
#define BL_SKETCH_RAM_SIZE      0x10000     /* 64 KB */

/* ---- Baseline only: 4 KB below the window survive a reset (the sketch's RTC
 * memory, Baseline's state). arduino-pico's OTA boot stub, which runs before
 * every start, uses the first 12 KB of RAM and 0x20035000-0x20040000 itself,
 * so this can't be the linker's .uninitialized_data. The heap ends below it
 * (flick sets arduino-pico's RAM length from it). */
#define BL_RP2_KEEP_BASE        0x2006D000

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). WiFi and Bluetooth come from a radio
 * module on some boards: their bl_board.h defines BL_HAS_WIFI / BL_HAS_BLE. */
#define BL_PLATFORM_RP2       1       /* arduino-pico names (later: rp2040.*, PIO) */

#define BL_TARGET_CHIP          "rp2350"      /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
