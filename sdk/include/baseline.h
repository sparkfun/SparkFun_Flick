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
 * <ctype.h> macros are not supported yet. Raw API: docs/API.md.
 */
#ifndef BASELINE_H
#define BASELINE_H

/* Like Arduino.h: the usual C headers come for free. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef putchar      /* newlib macros that need its stdio internals */
#undef getchar

#include "baseline_types.h"
#include "baseline_api.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

/* C++ sketches (.ino/.cpp): Serial, String, WiFi, HTTPClient, Wire, ESP, esp_now, ... */
#include "baseline_arduino.h"

#endif /* BASELINE_H */
