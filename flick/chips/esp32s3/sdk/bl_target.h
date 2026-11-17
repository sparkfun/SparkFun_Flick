/*
 * bl_target.h - ESP32-S3 chip, shared by every ESP32-S3 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32-S3"
#define BL_TARGET_CHIP_ID       9           /* == esp_chip_id_t ESP_CHIP_ID_ESP32S3 */
#define BL_TARGET_BOOT_PIN      0           /* the BOOT button (a strapping pin) */

/* ---- Flash window ------------------------------------------------------
 * The active sketch slot is mapped by the flash MMU 24 MB into the 32 MB
 * external-memory window. The S3's instruction bus (0x42xxxxxx) and data bus
 * (0x3Cxxxxxx) share one MMU table, so one mapping shows offset N of the slot
 * at IBUS_BASE+N (code) and DBUS_BASE+N (header, rodata, .data initializers). */
#define BL_SKETCH_IBUS_BASE     0x43800000
#define BL_SKETCH_DBUS_BASE     0x3D800000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Above the largest
 * Baseline static data area and below the ROM startup stack (0x3FCE9710). */
#define BL_SKETCH_RAM_BASE      0x3FCD9000
#define BL_SKETCH_RAM_SIZE      0x10000     /* 64 KB */

/* Touch pins T1-T14 (ESP32-S3: GPIO1-14) */
#define BL_TOUCH_T1 1
#define BL_TOUCH_T2 2
#define BL_TOUCH_T3 3
#define BL_TOUCH_T4 4
#define BL_TOUCH_T5 5
#define BL_TOUCH_T6 6
#define BL_TOUCH_T7 7
#define BL_TOUCH_T8 8
#define BL_TOUCH_T9 9
#define BL_TOUCH_T10 10
#define BL_TOUCH_T11 11
#define BL_TOUCH_T12 12
#define BL_TOUCH_T13 13
#define BL_TOUCH_T14 14

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_ESP32     1       /* Arduino-ESP32 / ESP-IDF names (esp_*, ESP, ledc*, ps_malloc, ...) */
#define BL_HAS_LED_STRIP      1       /* WS2812-type LEDs: rgbLedWrite, FastLED.h (RMT or bit-banged) */
#define BL_HAS_WIFI           1       /* WiFi, HTTP, web server, MQTT, mDNS, WiFi OTA */
#define BL_HAS_NET            1       /* sockets and HTTP (WiFiClient, WiFiUDP, HTTPClient) */
#define BL_HAS_TLS            1       /* WiFiClientSecure, HTTPS */
#define BL_HAS_ESPNOW         1       /* ESP-NOW (the chip's own radio) */
#define BL_HAS_BLE            1       /* Bluetooth LE (BLESerial, BLEDevice, ...) */
#define BL_HAS_TOUCH          1       /* capacitive touch pins (touchRead, T0...) */
#define BL_HAS_USB_DEVICE     1       /* USB device mode (USBHIDKeyboard, USBMIDI, ...) */

#define BL_TARGET_CHIP          "esp32s3"   /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
