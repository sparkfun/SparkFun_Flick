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

/* ---- sleep: a timed restart for now (bl_sleep_restart) ---- */
static uint64_t s_wake_us;

extern "C" int bl_impl_sleep_wake(int source, uint64_t value, int pin, int level)
{
    if (source != BL_WAKE_TIMER) {
        return BL_ERR_UNSUPPORTED;
    }
    s_wake_us = value;
    return BL_OK;
}

extern "C" int bl_impl_sleep_disable_wake(int source)
{
    if (source == BL_WAKE_TIMER || source == BL_WAKE_ALL) {
        s_wake_us = 0;
    }
    return BL_OK;
}

extern "C" void bl_impl_sleep_deep(void)
{
    if (!s_wake_us) {
        bl_impl_printf("deep sleep without a wake source is not supported on this chip\n");
        return;
    }
    bl_sleep_restart(s_wake_us);
}

extern "C" int bl_impl_sleep_wake_cause(void)
{
    return bl_wake_cause_code();
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
