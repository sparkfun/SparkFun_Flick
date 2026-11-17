/*
 * bl_teensy_impl.cpp - the API table entries Baseline implements on the
 * i.MX RT1062, on top of Teensyduino. Every other entry is a weak stub
 * (bl_teensy_weak.cpp), which answers like a chip without the hardware
 * (BL_ERR_UNSUPPORTED, 0, -1 or NULL).
 */
#include <malloc.h>
#include <stdarg.h>
#include "bl_teensy.h"

extern "C" {
extern unsigned long _heap_start, _heap_end;
extern uint8_t external_psram_size;     /* startup.c: MB of PSRAM found (Teensy 4.1) */
float tempmonGetTemp(void);
void extmem_free(void *ptr);
void *extmem_realloc(void *ptr, size_t size);
void *_sbrk(int incr);
}

/* ---- time ---- */
extern "C" uint32_t bl_impl_millis(void)
{
    return millis();
}

extern "C" uint64_t bl_impl_micros(void)
{
    static uint32_t last, high;     // Teensy's micros() wraps after 71 minutes
    uint32_t now = micros();
    if (now < last) {
        high++;
    }
    last = now;
    return (uint64_t)high << 32 | now;
}

extern "C" void bl_impl_delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    do {
        bl_poll();
        yield();
    } while (millis() - start < ms);
}

extern "C" void bl_impl_delay_us(uint32_t us)
{
    delayMicroseconds(us);
}

extern "C" void bl_impl_yield(void)
{
    bl_poll();
    yield();
}

/* ---- console / logging ---- */
extern "C" int bl_impl_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < (int)sizeof(buf)) {
        if (n > 0) {
            bl_console_out(buf, n);
        }
    } else {
        char *big = (char *)malloc(n + 1);
        if (big) {
            vsnprintf(big, n + 1, fmt, copy);
            bl_console_out(big, n);
            free(big);
        }
    }
    va_end(copy);
    return n;
}

extern "C" int bl_impl_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_impl_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

extern "C" int bl_impl_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    return vsnprintf(buf, size, fmt, ap);
}

extern "C" int bl_impl_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" void bl_impl_log(int level, const char *tag, const char *fmt, ...)
{
    static const char letters[] = "?EWIDV";
    bl_impl_printf("%c (%lu) %s: ", letters[level >= 1 && level <= 5 ? level : 0], (unsigned long)millis(),
                   tag ? tag : "");
    va_list ap;
    va_start(ap, fmt);
    bl_impl_vprintf(fmt, ap);
    va_end(ap);
    bl_console_out("\n", 1);
}

extern "C" int bl_impl_console_write(const uint8_t *data, size_t len)
{
    bl_console_out(data, len);
    return (int)len;
}

extern "C" int bl_impl_console_available(void)
{
    return bl_sketch_rx_available();
}

extern "C" int bl_impl_console_read(void)
{
    return bl_sketch_rx_read();
}

/* ---- memory: newlib's heap in OCRAM (RAM2) ---- */
extern "C" void *bl_impl_malloc(size_t size)
{
    return malloc(size);
}

extern "C" void *bl_impl_calloc(size_t n, size_t size)
{
    return calloc(n, size);
}

/* ps_malloc() memory (Teensy 4.1 PSRAM) comes from Teensy's extmem pool and
 * goes back there; sketches free both kinds with free(). */
extern "C" void *bl_impl_realloc(void *ptr, size_t size)
{
    return bl_is_psram(ptr) ? extmem_realloc(ptr, size) : realloc(ptr, size);
}

extern "C" void bl_impl_free(void *ptr)
{
    if (bl_is_psram(ptr)) {
        extmem_free(ptr);
    } else {
        free(ptr);
    }
}

extern "C" uint32_t bl_impl_free_heap(void)
{
    char *brk = (char *)_sbrk(0);
    struct mallinfo mi = mallinfo();
    return (uint32_t)((char *)&_heap_end - brk) + mi.fordblks;
}

/* ---- system ---- */
extern "C" void bl_impl_restart(void)
{
    bl_restart_clean();
}

extern "C" const char *bl_impl_baseline_version(void)
{
    return BASELINE_VERSION;
}

extern "C" uint32_t bl_impl_system_info(int what)
{
    switch (what) {
    case BL_INFO_CPU_MHZ:
        return F_CPU_ACTUAL / 1000000;
    case BL_INFO_HEAP_SIZE:
        return (uint32_t)((char *)&_heap_end - (char *)&_heap_start);
    case BL_INFO_MIN_FREE_HEAP:
    case BL_INFO_MAX_ALLOC_HEAP:
        return bl_impl_free_heap();
    case BL_INFO_PSRAM_SIZE:
        return (uint32_t)external_psram_size << 20;
    case BL_INFO_FREE_PSRAM:
        return 0;
    case BL_INFO_FLASH_SIZE:
        return BL_BOARD_FLASH_SIZE;
    default:
        return 0;
    }
}

extern "C" float bl_impl_temperature_read(void)
{
    return tempmonGetTemp();
}

extern "C" int bl_impl_reset_reason(void)
{
    return bl_reset_reason_code();
}

extern "C" int bl_impl_last_crash(char *buf, size_t size)
{
    return bl_last_crash_text(buf, size);
}

/* ---- sleep ----
 * Deep sleep: Baseline waits for a wake source, then restarts as an ESP32 wakes
 * (RTC memory survives); light sleep returns to the sketch. Wake sources: a
 * timer, one pin at a level (EXT0, or GPIO for light sleep) or several (EXT1).
 * BL_USE_SNOOZE 1 sleeps the chip in Snooze's low-power modes instead of
 * waiting; it is off because in Baseline the chip resets when Snooze's timer
 * should wake it (TESTING.md, deep sleep on Teensy). Waiting keeps USB alive,
 * so uploads work during a sleep. */
#ifndef BL_USE_SNOOZE
#define BL_USE_SNOOZE 0
#endif

#define WAKE_TIMER  4           /* esp_sleep_wakeup_cause_t */
#define WAKE_EXT0   2
#define WAKE_EXT1   3
#define WAKE_GPIO   7
#define TIMER_WAKE_CODE 36      /* Snooze's code for its timer (the waiting path uses it too) */

static uint64_t s_wake_us;
static int s_wake_pin = -1;     /* EXT0 / GPIO */
static uint64_t s_wake_mask;    /* EXT1 */
static bool s_wake_high;        /* the level that wakes */
static int s_light_cause;
static uint64_t s_ext1_status;

#if BL_USE_SNOOZE
#include <Snooze.h>
#define SLEEP_STEP_S 120        /* the watchdog keeps counting: stretch it, wake to feed it */
static SnoozeTimer s_snz_timer;
static SnoozeDigital s_snz_digital;
static SnoozeBlock s_snz(s_snz_timer, s_snz_digital);

/* Snooze.sleep until a pin wakes the chip or the whole time has passed.
 * Returns what Snooze returned last. */
static int sleep_steps(bool deep)
{
    (void)deep;
    uint64_t left_s = s_wake_us >= 1000000 ? (s_wake_us + 500000) / 1000000 : 0;
    BlWdogLong long_watchdog;
    int who;
    do {
        uint32_t step = left_s > SLEEP_STEP_S ? SLEEP_STEP_S : (uint32_t)left_s;
        if (step) {
            s_snz_timer.setTimer(step);
        } else {
            s_snz_timer.isUsed = false;     /* pins only */
        }
        who = Snooze.sleep(s_snz);
        bl_poll();
        if (who != TIMER_WAKE_CODE) {
            break;
        }
        left_s -= step;
    } while (left_s > 0);
    return who;
}
#else
/* Wait, serving USB and the loader, until a pin is at its level or the time is up. */
static int sleep_steps(bool deep)
{
    (void)deep;
    uint64_t waited_us = 0;
    uint32_t last = micros();
    for (;;) {
        bl_poll();
        if (s_wake_pin >= 0 && digitalRead(s_wake_pin) == (s_wake_high ? HIGH : LOW)) {
            return s_wake_pin;
        }
        for (int p = 0; s_wake_mask && p < CORE_NUM_DIGITAL && p < 64; p++) {
            if ((s_wake_mask & (1ULL << p)) && digitalRead(p) == (s_wake_high ? HIGH : LOW)) {
                return p;
            }
        }
        uint32_t now = micros();
        waited_us += (uint32_t)(now - last);
        last = now;
        if (s_wake_us && waited_us >= s_wake_us) {
            return TIMER_WAKE_CODE;
        }
        yield();
    }
}
#endif

static void wake_pin(int pin, bool high)
{
    s_wake_high = high;
#if BL_USE_SNOOZE
    s_snz_digital.pinMode(pin, high ? INPUT_PULLDOWN : INPUT_PULLUP, high ? RISING : FALLING);
#else
    pinMode(pin, high ? INPUT_PULLDOWN : INPUT_PULLUP);
#endif
}

extern "C" int bl_impl_sleep_wake(int source, uint64_t value, int pin, int level)
{
    switch (source) {
    case BL_WAKE_TIMER:
        s_wake_us = value;     /* the timer is set per step (sleep_steps) */
        return BL_OK;
    case BL_WAKE_EXT0:
    case BL_WAKE_GPIO:
        if (pin < 0 || pin > 32) {
            return BL_ERR_ARG;          /* pins 0-32 (Snooze's range on Teensy 4.x) */
        }
        s_wake_pin = pin;
        wake_pin(pin, level != 0);
        return BL_OK;
    case BL_WAKE_EXT1:      /* level: ESP_EXT1_WAKEUP_ANY_HIGH (1) or ALL_LOW (0, any low here) */
        s_wake_mask = value;
        for (int p = 0; p <= 32; p++) {     /* Snooze's range on Teensy 4.x */
            if (value & (1ULL << p)) {
                wake_pin(p, level != 0);
            }
        }
        return BL_OK;
    default:
        return BL_ERR_UNSUPPORTED;
    }
}

extern "C" int bl_impl_sleep_disable_wake(int source)
{
    if (source == BL_WAKE_TIMER || source == BL_WAKE_ALL) {
        s_wake_us = 0;
#if BL_USE_SNOOZE
        s_snz_timer.isUsed = false;     /* Snooze keeps a set timer armed otherwise */
#endif
    }
    if (source == BL_WAKE_EXT0 || source == BL_WAKE_GPIO || source == BL_WAKE_ALL) {
        s_wake_pin = -1;
    }
    if (source == BL_WAKE_EXT1 || source == BL_WAKE_ALL) {
        s_wake_mask = 0;
    }
    return BL_OK;
}

/* What Snooze returned: a pin number for a pin wake, else the timer. */
static int cause_of(int who)
{
    if (who >= 0 && who < 64 && (s_wake_mask & (1ULL << who))) {
        s_ext1_status = 1ULL << who;
        return WAKE_EXT1;
    }
    if (who == s_wake_pin && who >= 0) {
        return WAKE_EXT0;
    }
    return WAKE_TIMER;
}

extern "C" void bl_impl_sleep_deep(void)
{
    bool pins = s_wake_pin >= 0 || s_wake_mask;
    if (!s_wake_us && !pins) {
        bl_impl_printf("deep sleep needs a wake source (a timer or a pin)\n");
        return;
    }
    if (BL_USE_SNOOZE && !pins && s_wake_us < 1000000) {
        bl_sleep_restart(s_wake_us);    /* below Snooze's 1 s timer step */
    }
    Serial.flush();
    delay(10);
    int who = sleep_steps(true);
    bl_sleep_woke(cause_of(who));
}

extern "C" int bl_impl_sleep_light(void)
{
    if (!s_wake_us && s_wake_pin < 0 && !s_wake_mask) {
        return BL_ERR_STATE;
    }
    if (BL_USE_SNOOZE && s_wake_pin < 0 && !s_wake_mask && s_wake_us < 1000000) {
        delay((uint32_t)(s_wake_us / 1000));
        s_light_cause = WAKE_TIMER;
        return BL_OK;
    }
    Serial.flush();
    int who = sleep_steps(false);
    s_light_cause = cause_of(who);
    if (s_light_cause == WAKE_EXT0) {
        s_light_cause = WAKE_GPIO;
    }
    return BL_OK;
}

extern "C" int bl_impl_sleep_wake_cause(void)
{
    return s_light_cause ? s_light_cause : bl_wake_cause_code();
}

extern "C" uint64_t bl_impl_sleep_ext1_status(void)
{
    return s_ext1_status;
}

extern "C" void *bl_impl_rtc_memory(uint32_t *size)
{
    return bl_rtc_memory_block(size);
}

/* ---- GPIO ---- */
static bool pin_ok(int pin)
{
    return pin >= 0 && pin < CORE_NUM_DIGITAL;
}

extern "C" int bl_impl_gpio_mode(int pin, int mode)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    switch (mode) {
    case BL_INPUT:            pinMode(pin, INPUT); break;
    case BL_OUTPUT:           pinMode(pin, OUTPUT); break;
    case BL_INPUT_PULLUP:     pinMode(pin, INPUT_PULLUP); break;
    case BL_INPUT_PULLDOWN:   pinMode(pin, INPUT_PULLDOWN); break;
    case BL_OUTPUT_OPEN_DRAIN: pinMode(pin, OUTPUT_OPENDRAIN); break;
    default:                  return BL_ERR_ARG;
    }
    return BL_OK;
}

extern "C" int bl_impl_gpio_write(int pin, int level)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    digitalWrite(pin, level ? HIGH : LOW);
    return BL_OK;
}

extern "C" int bl_impl_gpio_read(int pin)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    return digitalRead(pin) ? BL_HIGH : 0;
}

/* ---- analog, PWM ---- */
extern "C" int bl_impl_adc_read(int pin)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    return analogRead(pin);     // 12 bits (bl_teensy_start sets the resolution)
}

extern "C" int bl_impl_adc_read_mv(int pin)
{
    int v = bl_impl_adc_read(pin);
    return v < 0 ? v : (int)((uint32_t)v * 3300 / 4095);
}

extern "C" int bl_impl_pwm_write(int pin, int value)
{
    if (!pin_ok(pin) || !digitalPinHasPWM(pin)) {
        return BL_ERR_ARG;
    }
    analogWrite(pin, constrain(value, 0, 255));
    return BL_OK;
}
