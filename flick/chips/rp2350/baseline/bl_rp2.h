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

#define BASELINE_VERSION "0.4.1"

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

/* bl_rp2_storage.cpp */
bool bl_lfs_mount(void);                    /* LittleFS (formats a blank region) */

/* bl_rp2_loader.cpp: a sketch image streamed into the free slot (ota_pull_sketch).
 * end() checks it and, if good, makes it active and restarts; else returns why not. */
void bl_sketch_stream_begin(void);
void bl_sketch_stream_write(const uint8_t *data, uint32_t len);
const char *bl_sketch_stream_end(void);

/* bl_rp2_update.cpp: Baseline updates over the air (docs/OTA.md). The loader's
 * UBEGIN/UDATA/UEND call the first three (NULL = OK, else the error text);
 * bl_update_install() writes the OTA command and restarts. */
const char *bl_update_begin(uint32_t size, const uint8_t sha256[32]);
const char *bl_update_data(uint32_t offset, const uint8_t *data, uint32_t len);
const char *bl_update_end(char *note, size_t note_size);
void bl_update_install(void) __attribute__((noreturn));
void bl_update_boot(void);                  /* bl_rp2_start(), before the sketch starts: trial, rollback */
void bl_update_poll(void);                  /* bl_poll(): confirms a new Baseline after 10 s */
void bl_update_confirm(void);               /* before a deliberate restart */

#ifdef __cplusplus
}
/* bl_rp2_net.cpp: a GET whose body streams into out. The HTTP status, or BL_ERR_*. */
int bl_http_get_to(const char *url, Print &out);
/* bl_rp2_net.cpp: starts NTP if nothing did and waits for the clock (HTTPS needs it). */
bool bl_ntp_ensure(uint32_t wait_ms);
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
void bl_bootsel_poll(void);                 /* bl_rp2_impl.cpp: BOOTSEL interrupts (pin BL_TARGET_BOOT_PIN), from bl_poll() */

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
