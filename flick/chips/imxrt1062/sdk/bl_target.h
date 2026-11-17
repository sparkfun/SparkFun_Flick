/*
 * bl_target.h - NXP i.MX RT1062 chip (Teensy 4.0, Teensy 4.1), shared by every
 * i.MX RT1062 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins,
 * where the sketch slots sit in flash) are in the product target's bl_board.h,
 * included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "i.MX RT1062"
#define BL_TARGET_CHIP_ID       0x1062      /* not an esp_chip_id_t: Espressif's IDs are small numbers */
#define BL_TARGET_BOOT_PIN      (-1)        /* none: the PROGRAM button belongs to the bootloader chip */

/* ---- Flash window ------------------------------------------------------
 * The Cortex-M7 runs code straight from QSPI flash, which FlexSPI maps from
 * 0x60000000 (2 MB on the Teensy 4.0, 8 MB on the 4.1; Teensy's MPU makes the
 * first 16 MB cached and executable). There is no MMU: Baseline points the
 * window below at the active sketch slot with FlexSPI's address remap
 * (IOMUXC_GPR30/31/32), so sketches are linked once, at the window, whichever
 * slot they land in. The window is 12 MB in: past the flash of every
 * Teensy 4.x, inside the MPU's 16 MB region. Code and data share it. */
#define BL_SKETCH_IBUS_BASE     0x60C00000
#define BL_SKETCH_DBUS_BASE     0x60C00000
/* BL_SKETCH_MAX_SIZE is the board's (bl_board.h): its slot size, which depends
 * on the flash. */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, in DTCM (single-cycle, not cached, where Teensy keeps
 * globals too). Baseline's own .data/.bss sit below it and its stack above;
 * its linker script checks both. */
#define BL_SKETCH_RAM_BASE      0x20020000
#define BL_SKETCH_RAM_SIZE      0x20000     /* 128 KB */

/* ---- Baseline only: the top 2 KB of OCRAM survive a reset (uncached through
 * an MPU region): the sketch's RTC memory (1 KB), Baseline's state, and
 * Teensy's CrashReport at 0x2027FF80. The heap ends below. */
#define BL_TEENSY_KEEP_BASE     0x2027F800

/* Sketches may run their own interrupt handlers here (attachInterrupt,
 * IntervalTimer, attachInterruptVector): flash is always readable, except while
 * it is written, and then every interrupt is off. */
#define BL_TARGET_SKETCH_ISR    1
#define BL_TARGET_TEENSY        1       /* the SDK adds baseline_teensy.h */

/* Teensy's interrupt vector table in RAM (_VectorsRam, SCB_VTOR), first in
 * DTCM. Part of the ABI: sketches link _VectorsRam to it (rom.ld), so
 * imxrt.h's attachInterruptVector() works in them. */
#define BL_TEENSY_VECTORS       0x20000000

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_TEENSY    1       /* Teensyduino names (IntervalTimer, extmem_malloc, analogWriteFrequency, ...) */
#define BL_HAS_AUDIO          1       /* the Teensy Audio library (Audio.h) */
#define BL_HAS_LED_STRIP      1       /* WS2812-type LEDs on any pin: rgbLedWrite, FastLED.h (bit-banged) */
#define BL_HAS_USB_HOST       1       /* the USB host port (USBHost_t36.h): keyboards, mice, joysticks, MIDI, USB-serial, drives */

#define BL_TARGET_CHIP          "imxrt1062"   /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
