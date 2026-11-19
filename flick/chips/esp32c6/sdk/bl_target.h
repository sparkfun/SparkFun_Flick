/*
 * bl_target.h - ESP32-C6 chip, shared by every ESP32-C6 product target.
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this chip. Board facts (name, pins)
 * are in the product target's bl_board.h, included at the end.
 * NOT YET RUN ON HARDWARE.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_CHIP_NAME     "ESP32-C6"
#define BL_TARGET_CHIP_ID       13          /* == esp_chip_id_t ESP_CHIP_ID_ESP32C6 */
#define BL_TARGET_BOOT_PIN      9           /* the BOOT button (a strapping pin) */

/* ---- Flash window ------------------------------------------------------
 * The C6 maps flash for instructions and data at the same addresses
 * (SOC_MMU_DI_VADDR_SHARED): 16 MB from 0x42000000, 256 entries of 64 KB. The
 * sketch window is the top 1 MB, so IBUS_BASE == DBUS_BASE. */
#define BL_SKETCH_IBUS_BASE     0x42F00000
#define BL_SKETCH_DBUS_BASE     0x42F00000
#define BL_SKETCH_MAX_SIZE      0x100000    /* 1 MB */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss, reserved out of the heap by Baseline. Just below the
 * end of the static-data region (SRAM_SEG_END 0x4086E610 in IDF memory.ld).
 * CHECK _heap_start after each Baseline build. */
#define BL_SKETCH_RAM_BASE      0x40866000
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

#define BL_TARGET_CHIP          "esp32c6"     /* chips/<chip> */

#include "bl_board.h"                 /* targets/<target>/sdk/bl_board.h */

#endif /* BL_TARGET_H */
