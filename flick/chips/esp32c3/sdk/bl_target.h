/*
 * bl_target.h - ESP32-C3 chip, shared by every ESP32-C3 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 * NOT YET RUN ON HARDWARE.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32-C3"
#define BL_TARGET_CHIP_ID       5          /* == esp_chip_id_t ESP_CHIP_ID_ESP32C3 */
#define BL_TARGET_BOOT_PIN      9           /* the BOOT button (a strapping pin) */

/* ---- Flash window ------------------------------------------------------
 * Separate I-bus (0x42xxxxxx) and D-bus (0x3Cxxxxxx) windows of 8 MB
 * that share one MMU table (like the ESP32-S3), so one mapping shows offset N of
 * the slot at IBUS_BASE+N (code) and DBUS_BASE+N (header, rodata, .data
 * initializers). The window is the top 512 KB (MMU entries 120-127); Baseline's
 * own code and rodata start at the bottom. */
#define BL_SKETCH_IBUS_BASE     0x42780000
#define BL_SKETCH_DBUS_BASE     0x3C780000
#define BL_SKETCH_MAX_SIZE      0x80000     /* 512 KB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Just below the
 * end of the static-data region (SRAM_DRAM_END 0x3FCCE710 in IDF memory.ld),
 * above Baseline's static data. CHECK _heap_start after each Baseline build. */
#define BL_SKETCH_RAM_BASE      0x3FCC6000
#define BL_SKETCH_RAM_SIZE      0x8000      /* 32 KB */

/* ---- What the chip has: the SDK declares only the matching APIs, so a sketch
 * that uses something this chip lacks fails to compile with a clear message
 * (sdk/include/baseline_features.h). */
#define BL_PLATFORM_ESP32     1       /* Arduino-ESP32 / ESP-IDF names (esp_*, ESP, ledc*, ps_malloc, ...) */
#define BL_HAS_LED_STRIP      1       /* WS2812-type LEDs: rgbLedWrite, FastLED.h (RMT or bit-banged) */
#define BL_HAS_RTOS           1       /* FreeRTOS tasks, semaphores, queues (xTaskCreate, ...) */
#define BL_HAS_WIFI           1       /* WiFi, HTTP, web server, MQTT, mDNS, WiFi OTA */
#define BL_HAS_NET            1       /* sockets and HTTP (WiFiClient, WiFiUDP, HTTPClient) */
#define BL_HAS_TLS            1       /* WiFiClientSecure, HTTPS */
#define BL_HAS_ESPNOW         1       /* ESP-NOW (the chip's own radio) */
#define BL_HAS_BLE            1       /* Bluetooth LE (BLESerial, BLEDevice, ...) */

#define BL_TARGET_CHIP          "esp32c3"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
