/*
 * baseline.h - The one header a sketch includes. .ino files get it
 * automatically, and the classic Arduino headers (<Arduino.h>, <WiFi.h>,
 * <HTTPClient.h>, <Wire.h>, <esp_now.h>, ...) in sdk/include/compat all lead
 * here, so ordinary Arduino code compiles unchanged. Every call routes
 * through the Baseline firmware already on the board.
 *
 * Optional: void teardown(void) runs before the sketch is unloaded.
 *
 * C library: <string.h> basics come from the chip ROM (memcpy, memset,
 * memmove, memcmp, memchr, str[n]cpy, str[n]cmp, strlen, strnlen, strchr,
 * strrchr, strstr, str[n]cat, str[n]casecmp, abs, labs, qsort). malloc/free,
 * printf/snprintf/sprintf, puts, atoi/atof/strtol/strtod are forwarded to
 * Baseline by sdk/src/sketch_libc.c. libgcc supplies integer/float helpers.
 * <ctype.h> (isdigit, toupper, ...) is ASCII-only, as in the "C" locale. Raw API: docs/API.md.
 */
#ifndef BASELINE_H
#define BASELINE_H

/* Like Arduino.h: the usual C headers come for free. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>       /* NAN, sqrt, pow, ... (libm is linked) */
#include <ctype.h>      /* sdk/include/compat/ctype.h: isdigit, toupper, ... */
#undef putchar      /* newlib macros that need its stdio internals */
#undef getchar
#include "baseline_pgmspace.h"   /* PROGMEM, pgm_read_byte, ... (no-ops on ESP32) */

#include "baseline_types.h"
#include "baseline_api.h"
#include "bl_target.h"          /* the board's pins (targets/<target>/sdk/bl_board.h) */

#ifdef __cplusplus
extern "C" {
#endif

/* digitalWrite, digitalRead, analogWrite. On a board whose only LED is the
 * WS2812 (LED_BUILTIN == RGB_BUILTIN), they drive that LED in white, as
 * Arduino-ESP32 does on such boards, so blink sketches work everywhere. */
#ifndef RGB_BRIGHTNESS
#define RGB_BRIGHTNESS 64       /* Arduino-ESP32's brightness for digitalWrite(RGB_BUILTIN, HIGH) */
#endif
#if defined(BL_BOARD_RGB_LED) && BL_BOARD_LED_BUILTIN == BL_BOARD_RGB_LED
#define BL_SDK_RGB_ONLY_LED 1
extern uint8_t bl_sdk_led_level;   /* sketch_crt.c: the level last written, for digitalRead */
#endif
static inline int digitalWrite(int pin, int level)
{
#ifdef BL_SDK_RGB_ONLY_LED
    if (pin == BL_BOARD_RGB_LED) {
        uint8_t v = level ? RGB_BRIGHTNESS : 0;
        bl_sdk_led_level = level ? 1 : 0;
        return BL->rgb_write(pin, v, v, v);
    }
#endif
    return BL->gpio_write(pin, level);
}
static inline int digitalRead(int pin)
{
#ifdef BL_SDK_RGB_ONLY_LED
    if (pin == BL_BOARD_RGB_LED) {
        return bl_sdk_led_level;
    }
#endif
    return BL->gpio_read(pin);
}
static inline int analogWrite(int pin, int value)
{
#ifdef BL_SDK_RGB_ONLY_LED
    if (pin == BL_BOARD_RGB_LED) {
        uint8_t v = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
        bl_sdk_led_level = v ? 1 : 0;
        return BL->rgb_write(pin, v, v, v);
    }
#endif
    return BL->pwm_write(pin, value);
}

/* Implemented by the sketch. */
void setup(void);
void loop(void);
void teardown(void);

/* Implemented by the SDK (sketch_crt.c). While the sketch waits in delay() or
 * yield(), or between loop() calls, the SDK delivers callbacks the sketch
 * registered (for example esp_now_register_recv_cb), always on the sketch's
 * own task. */
void delay(uint32_t ms);
void yield(void);
void bl_sketch_add_pump(void (*pump)(void));   /* SDK-internal: register a callback source */

/* Time zone used by localtime()/mktime()/strftime() (sdk/src/sketch_time.c). */
int  bl_sdk_tz_set(const char *posix_tz);      /* e.g. "MST7MDT,M3.2.0,M11.1.0" */
void bl_sdk_tz_set_offsets(long gmt_offset_sec, int dst_offset_sec);

/* Arduino-style names. Define BL_NO_ARDUINO_NAMES to leave them out. */
#ifndef BL_NO_ARDUINO_NAMES
#define LOW               BL_LOW
#define HIGH              BL_HIGH
#define INPUT             BL_INPUT
#define OUTPUT            BL_OUTPUT
#define INPUT_PULLUP      BL_INPUT_PULLUP
#define INPUT_PULLDOWN    BL_INPUT_PULLDOWN
#define OUTPUT_OPEN_DRAIN BL_OUTPUT_OPEN_DRAIN
#endif

/* ---- Logging: Arduino (log_i), ESP-IDF (ESP_LOGI), and Baseline names ---- */
#ifndef BL_TAG
#define BL_TAG "sketch"
#endif
#define log_e(fmt, ...) bl_log(BL_LOG_ERROR,   BL_TAG, fmt, ##__VA_ARGS__)
#define log_w(fmt, ...) bl_log(BL_LOG_WARN,    BL_TAG, fmt, ##__VA_ARGS__)
#define log_i(fmt, ...) bl_log(BL_LOG_INFO,    BL_TAG, fmt, ##__VA_ARGS__)
#define log_d(fmt, ...) bl_log(BL_LOG_DEBUG,   BL_TAG, fmt, ##__VA_ARGS__)
#define log_v(fmt, ...) bl_log(BL_LOG_VERBOSE, BL_TAG, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) bl_log(BL_LOG_ERROR,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) bl_log(BL_LOG_WARN,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) bl_log(BL_LOG_INFO,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) bl_log(BL_LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) bl_log(BL_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)
#define BL_LOGE(...) log_e(__VA_ARGS__)
#define BL_LOGW(...) log_w(__VA_ARGS__)
#define BL_LOGI(...) log_i(__VA_ARGS__)
#define BL_LOGD(...) log_d(__VA_ARGS__)

/* ---- Misc Arduino / ESP-IDF names ---- */
static inline uint32_t getCpuFrequencyMhz(void) { return bl_system_info(BL_INFO_CPU_MHZ); }
static inline uint32_t esp_random(void) { return bl_random(); }
static inline void esp_restart(void) { bl_restart(); }

/* PSRAM: ps_malloc/ps_calloc/ps_realloc come from baseline_api.h. Plain malloc()
 * already puts blocks over 4 KB in PSRAM when the board has it. */
static inline bool psramFound(void) { return bl_system_info(BL_INFO_PSRAM_SIZE) != 0; }

#ifdef __cplusplus
}
#endif

/* C++ sketches (.ino/.cpp): Serial, String, WiFi, HTTPClient, Wire, ESP, esp_now, ... */
#include "baseline_arduino.h"

#endif /* BASELINE_H */
