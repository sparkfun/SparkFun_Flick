/*
 * bl_apollo3.h - Baseline for the Apollo3 (SparkFun Artemis), on SparkFun's
 * Apollo3 Arduino core (Mbed OS 6 with the RTX kernel).
 *
 * The sketch's setup() and loop() run on an Mbed thread of their own, with the
 * stack size the sketch's header asks for. Baseline services the console (and
 * with it the @BSK loader) between loop() calls and inside delay(), yield()
 * and the console calls, through bl_poll(), always on that thread; with no
 * sketch running, Arduino's loop() on the main thread does it.
 */
#pragma once

#ifdef __cplusplus
#include <Arduino.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif
#include "bl_impl.h"
#include "sketch_abi.h"

#define BASELINE_VERSION "0.4.1"

#ifdef __cplusplus
extern "C" {
#endif

/* bl_apollo3_loader.cpp */
void bl_apollo3_start(void);
void bl_apollo3_loop(void);
void bl_poll(void);                         /* console, loader, watchdog */
int bl_sketch_rx_available(void);           /* console input that is not for the loader */
int bl_sketch_rx_read(void);
void bl_console_out(const void *data, size_t len);
void bl_restart_clean(void) __attribute__((noreturn));
int bl_last_crash_text(char *buf, size_t size);
int bl_reset_reason_code(void);
void *bl_rtc_memory_block(uint32_t *size);
int bl_wake_cause_code(void);
/* Deep sleep: waits (us 0: no timer; pin -1: no EXT0 pin), then the sketch
 * restarts without a chip reset. Called on the sketch's thread. */
void bl_sleep_restart(uint64_t us, int pin, int level) __attribute__((noreturn));
void bl_wdog_long(bool on);
void bl_flash_erase(uint32_t addr, uint32_t len);               /* 8 KB pages */
void bl_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);  /* words; data in RAM */

/* Called by bl_poll() and when a sketch restarts without a chip reset (deep sleep). */
void bl_timers_poll(void);                  /* bl_apollo3_hw.cpp: fires due timers (Ticker) */
void bl_hw_reset(void);                     /* bl_apollo3_hw.cpp: timers, PWM, buses, Serial1 stop */
void bl_storage_reset(void);                /* bl_apollo3_storage.cpp: files and Preferences close */
void bl_gpio_reset(void);                   /* bl_apollo3_impl.cpp: interrupts detach, wake sources clear */
void bl_sketch_heap_release(void);          /* bl_apollo3_impl.cpp: the sketch's heap blocks are freed */
void bl_ble_poll(void);                     /* bl_apollo3_ble.cpp: BLE input, from bl_poll() */
void bl_ble_reset(void);                    /* bl_apollo3_ble.cpp: advertising, connections, the GATT database go */

/* bl_sha256.c */
typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t buf[64];
    uint32_t used;
} bl_sha256_t;
void bl_sha256_init(bl_sha256_t *c);
void bl_sha256_update(bl_sha256_t *c, const void *data, size_t len);
void bl_sha256_final(bl_sha256_t *c, uint8_t out[32]);

#ifdef __cplusplus
}
/* For the length of a call that can take seconds: the watchdog is off. */
struct BlWdogLong {
    BlWdogLong() { bl_wdog_long(true); }
    ~BlWdogLong() { bl_wdog_long(false); }
};
#endif
