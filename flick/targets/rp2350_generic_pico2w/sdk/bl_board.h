/*
 * bl_board.h - generic RP2350 with a CYW43439 radio, 4 MB flash (Raspberry Pi Pico 2 W).
 * RP2350A (Arm cores), 4 MB flash, no PSRAM, CYW43439 (WiFi 4, Bluetooth 5.2) on
 * PIO SPI, native USB.
 * Pins: arduino-pico's rpipico2w variant. Not yet run on hardware.
 * LED_BUILTIN is the LED on the radio's GPIO 0 (arduino-pico's pin 64), as on
 * the Thing Plus - RP2350.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "rp2350_generic_pico2w"
#define BL_BOARD_NAME           "Generic RP2350 with radio (Raspberry Pi Pico 2 W)"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    64  /* LED on the radio (CYW43 GPIO 0) */
#define BL_BOARD_STAT_LED       64
#define BL_BOARD_SDA            4   /* I2C0 */
#define BL_BOARD_SCL            5
#define BL_BOARD_A0             26
#define BL_BOARD_A1             27
#define BL_BOARD_A2             28
#define BL_BOARD_SCK            18  /* SPI0 */
#define BL_BOARD_MISO           16
#define BL_BOARD_MOSI           19
#define BL_BOARD_SS             17

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           1           /* UART0 */
#define BL_SERIAL1_TX           0
#define BL_SERIAL2_RX           9           /* UART1 */
#define BL_SERIAL2_TX           8

/* ---- Features of this board (the chip has no radio of its own: a CYW43439) ---- */
#define BL_HAS_WIFI             1       /* WiFi, HTTP(S), web server, MQTT, mDNS, WiFi uploads */
#define BL_HAS_NET              1       /* sockets and HTTP (over WiFi) */
#define BL_HAS_TLS              1       /* WiFiClientSecure, https:// */
#define BL_HAS_BLE              1       /* Bluetooth LE: BLE serial, GATT server and client, scanning (BTstack) */

/* ---- Flash (XIP addresses; flick passes the first four to arduino-pico) ----
 * 0x10000000  Baseline, up to 1.75 MB (with the radio's firmware)
 * 0x101C0000  sketch slot A, 512 KB  } also reached through the sketch window
 * 0x10240000  sketch slot B, 512 KB  } (ATRANS3, 0x10C00000)
 * 0x102C0000  LittleFS, 1.25 MB - 4 KB
 * 0x103FF000  arduino-pico's EEPROM sector (Baseline's state: active slot, trial) */
#define BL_BOARD_FLASH_SIZE     0x400000
#define BL_BOARD_BASELINE_MAX   0x1C0000
#define BL_BOARD_FS_START       0x102C0000
#define BL_BOARD_FS_END         0x103FF000
#define BL_BOARD_EEPROM_START   0x103FF000
#define BL_BOARD_SLOT_A         0x001C0000  /* flash offsets */
#define BL_BOARD_SLOT_B         0x00240000
#define BL_BOARD_SLOT_SIZE      0x80000
