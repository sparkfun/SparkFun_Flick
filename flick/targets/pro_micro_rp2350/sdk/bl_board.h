/*
 * bl_board.h - SparkFun Pro Micro - RP2350.
 * RP2350A (Arm cores), 16 MB flash, 8 MB PSRAM (CS on GPIO 19), native USB-C, Qwiic.
 * Pins: arduino-pico's sparkfun_promicrorp2350 variant. Checked on hardware 2026-11-17
 * (flick test 30/0/4; LED colors by eye).
 * The only LED is the WS2812 on GPIO 25, so LED_BUILTIN points at it.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "pro_micro_rp2350"
#define BL_BOARD_NAME           "SparkFun Pro Micro - RP2350"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    25  /* the WS2812 (no plain LED) */
#define BL_BOARD_STAT_LED       25
#define BL_BOARD_RGB_LED        25  /* WS2812 */
#define BL_BOARD_SDA            16  /* Qwiic (I2C0) */
#define BL_BOARD_SCL            17
#define BL_BOARD_A0             26
#define BL_BOARD_A1             27
#define BL_BOARD_A2             28
#define BL_BOARD_A3             29
#define BL_BOARD_SCK            22
#define BL_BOARD_MISO           20  /* POCI */
#define BL_BOARD_MOSI           23  /* PICO */
#define BL_BOARD_SS             21

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           1           /* UART0 */
#define BL_SERIAL1_TX           0
#define BL_SERIAL2_RX           9           /* UART1 */
#define BL_SERIAL2_TX           8
#define BL_BOARD_PSRAM_CS       19

/* ---- Flash (XIP addresses; flick passes the first four to arduino-pico) ----
 * 0x10000000  Baseline, up to 4 MB
 * 0x10400000  LittleFS, 8 MB - 4 KB
 * 0x10BFF000  arduino-pico's EEPROM sector (Baseline's state: active slot, trial)
 * 0x10C00000  sketch slot A, 512 KB  } the last 4 MB: reached only through the
 * 0x10C80000  sketch slot B, 512 KB  } sketch window (ATRANS3), never directly
 * 0x10D00000  free, 3 MB */
#define BL_BOARD_FLASH_SIZE     0x1000000
#define BL_BOARD_BASELINE_MAX   0x400000
#define BL_BOARD_FS_START       0x10400000
#define BL_BOARD_FS_END         0x10BFF000
#define BL_BOARD_EEPROM_START   0x10BFF000
#define BL_BOARD_SLOT_A         0x00C00000  /* flash offsets */
#define BL_BOARD_SLOT_B         0x00C80000
#define BL_BOARD_SLOT_SIZE      0x80000
