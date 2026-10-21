/*
 * bl_target.h - ESP32-S3 target (SparkFun ESP32-S3 Thing Plus).
 *
 * Included by sdk/include/sketch_abi.h, so it is seen by Baseline, the sketch
 * SDK, and the sketch linker script (via the C preprocessor). #defines only.
 * The addresses are part of the ABI for this target.
 */
#ifndef BL_TARGET_H
#define BL_TARGET_H

#define BL_TARGET_NAME          "esp32s3"
#define BL_TARGET_CHIP_NAME     "ESP32-S3"
#define BL_TARGET_CHIP_ID       9           /* == esp_chip_id_t ESP_CHIP_ID_ESP32S3 */

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

/* ---- Board pins seen by sketches (LED_BUILTIN, SDA, ...) ---- */
#define BL_BOARD_NAME           "SparkFun ESP32-S3 Thing Plus"
#define BL_BOARD_LED_BUILTIN    0           /* blue STAT LED */
#define BL_BOARD_STAT_LED       0
#define BL_BOARD_RGB_LED        46          /* WS2812 */
#define BL_BOARD_SDA            8           /* Qwiic */
#define BL_BOARD_SCL            9
#define BL_BOARD_A0             10

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
#define BL_HAS_USB_DEVICE 1         /* USB keyboard/mouse/gamepad/MIDI */

#endif /* BL_TARGET_H */
