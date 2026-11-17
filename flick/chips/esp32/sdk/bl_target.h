/*
 * bl_target.h - classic ESP32 chip, shared by every classic-ESP32 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32"
#define BL_TARGET_CHIP_ID       0           /* == esp_chip_id_t ESP_CHIP_ID_ESP32 */
#define BL_TARGET_BOOT_PIN      0           /* the BOOT button (a strapping pin) */

/* ---- Flash window ------------------------------------------------------
 * The classic ESP32 has separate MMU entries for instructions and data, and a
 * table per CPU, so Baseline maps the active slot four times: the IRAM0 and
 * DROM0 windows below, on both cores. Offset N of the slot then appears at
 * IBUS_BASE+N (code) and DBUS_BASE+N (header, rodata, .data initializers).
 * Both windows are the top 512 KB of their regions (MMU entries 120-127 and
 * 56-63); Baseline's own code and rodata start at the bottom. */
#define BL_SKETCH_IBUS_BASE     0x40380000
#define BL_SKETCH_DBUS_BASE     0x3F780000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Static DRAM ends
 * by 0x3FFDC200 at most (memory.ld), and ROM keeps 0x3FFE0000-0x3FFE0440 and
 * 0x3FFE3F20-0x3FFE4350, so this window sits above both. */
#define BL_SKETCH_RAM_BASE      0x3FFE8000
#define BL_SKETCH_RAM_SIZE      0x8000      /* 32 KB */

/* Touch pins T0-T9 (classic ESP32) */
#define BL_TOUCH_T0 4
#define BL_TOUCH_T1 0
#define BL_TOUCH_T2 2
#define BL_TOUCH_T3 15
#define BL_TOUCH_T4 13
#define BL_TOUCH_T5 12
#define BL_TOUCH_T6 14
#define BL_TOUCH_T7 27
#define BL_TOUCH_T8 33
#define BL_TOUCH_T9 32
#define BL_HAS_DAC  1               /* DAC1 = GPIO25, DAC2 = GPIO26 */

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_ESP32     1       /* Arduino-ESP32 / ESP-IDF names (esp_*, ESP, ledc*, ps_malloc, ...) */
#define BL_HAS_WIFI           1       /* WiFi, HTTP, web server, MQTT, mDNS, WiFi OTA */
#define BL_HAS_ESPNOW         1       /* ESP-NOW (the chip's own radio) */
#define BL_HAS_BLE            1       /* Bluetooth LE (BLESerial, BLEDevice, ...) */
#define BL_HAS_BT_CLASSIC     1       /* Bluetooth Classic (BluetoothSerial) */
#define BL_HAS_TOUCH          1       /* capacitive touch pins (touchRead, T0...) */
#define BL_HAS_DAC            1       /* DAC outputs (dacWrite) */

#define BL_TARGET_CHIP          "esp32"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
