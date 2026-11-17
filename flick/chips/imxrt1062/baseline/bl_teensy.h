/*
 * bl_teensy.h - Baseline for the i.MX RT1062 (Teensy 4.x).
 *
 * There is no RTOS: the sketch's setup() and loop() run on Teensy's main
 * stack, called from Baseline's loop(). Baseline services the USB console
 * (and with it the @BSK loader) between loop() calls and inside delay(),
 * yield() and the console calls, through bl_poll().
 */
#pragma once

#include <Arduino.h>
#include "bl_impl.h"
#include "sketch_abi.h"

#define BASELINE_VERSION "0.3.0"

#ifdef __cplusplus
extern "C" {
#endif

/* bl_teensy_loader.cpp */
void bl_teensy_start(void);
void bl_teensy_loop(void);
void bl_poll(void);                         /* console, loader, watchdog */
int bl_sketch_rx_available(void);           /* console input that is not for the loader */
int bl_sketch_rx_read(void);
void bl_console_out(const void *data, size_t len);
void bl_restart_clean(void) __attribute__((noreturn));
int bl_last_crash_text(char *buf, size_t size);
int bl_reset_reason_code(void);
void *bl_rtc_memory_block(uint32_t *size);
int bl_wake_cause_code(void);
void bl_sleep_restart(uint64_t us) __attribute__((noreturn));
void bl_sleep_woke(int cause) __attribute__((noreturn));

void bl_wdog_long(bool on);

#ifdef __cplusplus
}
/* For the length of a call that can take seconds: the watchdog waits 128 s, not 5. */
struct BlWdogLong {
    BlWdogLong() { bl_wdog_long(true); }
    ~BlWdogLong() { bl_wdog_long(false); }
};
extern "C" {
#endif

/* bl_teensy_usbhost.cpp */
void bl_usbh_poll(void);                    /* USB host enumeration: from bl_poll() */

/* bl_teensy_hw.cpp */
void bl_timers_poll(void);                  /* fires due timers (Ticker): from bl_poll() */
bool bl_is_psram(const void *p);

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
#endif
