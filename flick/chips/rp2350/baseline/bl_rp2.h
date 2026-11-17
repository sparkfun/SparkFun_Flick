/*
 * bl_rp2.h - Baseline for the RP2350 (and later the RP2040), on arduino-pico.
 *
 * There is no RTOS: the sketch's setup() and loop() run on core 0, called from
 * Baseline's loop(), on a stack of their own (the process stack, PSP, sized by
 * the sketch's header). Baseline services the USB console (and with it the
 * @BSK loader) between loop() calls and inside delay(), yield() and the
 * console calls, through bl_poll().
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

#define BASELINE_VERSION "0.3.0"

#ifdef __cplusplus
extern "C" {
#endif

/* bl_rp2_loader.cpp */
void bl_rp2_start(void);
void bl_rp2_loop(void);
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
void bl_wdog_long(bool on);
void bl_flash_erase(uint32_t offset, uint32_t len);            /* flash offsets, 4 KB aligned */
void bl_flash_write(uint32_t offset, const uint8_t *data, uint32_t len);
/* Runs one "@BSK ..." line with the replies sent to sink (a WiFi upload). */
typedef void (*bl_reply_sink_t)(const char *text, size_t len, void *ctx);
void bl_loader_line(char *line, bl_reply_sink_t sink, void *ctx);
/* Calls fn(arg) on Baseline's 16 KB stack when called from the sketch (TLS). */
void bl_on_big_stack(void (*fn)(void *), void *arg);

/* bl_rp2_net.cpp */
void bl_net_poll(void);                     /* mDNS, DNS server, MQTT, WiFi uploads, events */

#ifdef __cplusplus
}
/* bl_on_big_stack() for a lambda: on_big_stack([&] { ... }); */
template <typename F> static inline void on_big_stack(F &&f)
{
    using Fn = typename std::remove_reference<F>::type;
    bl_on_big_stack([](void *p) { (*static_cast<Fn *>(p))(); }, (void *)&f);
}
/* For the length of a call that can take seconds (formatting LittleFS, a
 * microSD card that isn't there): the watchdog is off. */
struct BlWdogLong {
    BlWdogLong() { bl_wdog_long(true); }
    ~BlWdogLong() { bl_wdog_long(false); }
};
extern "C" {
#endif

/* bl_rp2_hw.cpp */
void bl_timers_poll(void);                  /* fires due timers (Ticker): from bl_poll() */

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
