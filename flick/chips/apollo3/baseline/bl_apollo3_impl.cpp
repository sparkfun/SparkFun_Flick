/*
 * bl_apollo3_impl.cpp - the API table entries Baseline implements on the
 * Apollo3, on top of SparkFun's Apollo3 core: time, console, memory, system,
 * GPIO (with deferred interrupts), ADC and PWM. Every other entry is a weak
 * stub (bl_apollo3_weak.cpp), which answers like a chip without the hardware
 * (BL_ERR_UNSUPPORTED, 0, -1 or NULL).
 */
#include <malloc.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <mbed.h>
#include "am_mcu_apollo.h"
#include "bl_apollo3.h"

/* Mbed wraps the printf family (-Wl,--wrap) with a minimal printf that has no
 * %f; Baseline formats with newlib's own (flick links _printf_float). */
extern "C" int __real_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
#define vsnprintf __real_vsnprintf

/* ---- time ---- */
extern "C" uint32_t bl_impl_millis(void)
{
    return millis();
}

extern "C" uint64_t bl_impl_micros(void)
{
    return ticker_read_us(get_us_ticker_data());    // Mbed's 64-bit microsecond count
}

/* The date and time (UTC): kept by Baseline from a time the sketch sets. */
static int64_t s_epoch_us;          /* wall clock minus the uptime, in us */
static bool s_time_set;

static uint64_t uptime_us(void)
{
    return (uint64_t)rtos::Kernel::Clock::now().time_since_epoch().count() * 1000ull;     // RTX tick: 1 ms
}

extern "C" int bl_impl_time_get(int64_t *sec, int32_t *usec)
{
    int64_t now = s_epoch_us + (int64_t)uptime_us();
    if (sec) {
        *sec = now / 1000000;
    }
    if (usec) {
        *usec = (int32_t)(now % 1000000);
    }
    return BL_OK;
}

extern "C" int bl_impl_time_set(int64_t sec, int32_t usec)
{
    s_epoch_us = sec * 1000000 + usec - (int64_t)uptime_us();
    s_time_set = true;
    return BL_OK;
}

extern "C" int bl_impl_time_synced(void)
{
    return s_time_set;
}

/* delay(): polls the loader every millisecond and lets other threads run. */
extern "C" void bl_impl_delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    for (;;) {
        bl_poll();
        uint32_t gone = millis() - start;
        if (gone >= ms) {
            break;
        }
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(ms - gone > 2 ? 2 : ms - gone));
    }
}

extern "C" void bl_impl_delay_us(uint32_t us)
{
    delayMicroseconds(us);
}

extern "C" void bl_impl_yield(void)
{
    bl_poll();
    rtos::ThisThread::yield();
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
    int n = __real_vsnprintf(buf, size, fmt, ap);
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

/* ---- memory: the core's heap (newlib malloc under Mbed's lock) ----
 * The sketch's blocks are kept in a list (an 8-byte header each, so blocks stay
 * 8-byte aligned): a sketch restarted after deep sleep without a chip reset
 * (bl_apollo3_loader.cpp) gets its heap back, as after a real reset. Only the
 * sketch calls these; Baseline's own allocations use malloc() directly. */
struct SketchBlock {
    SketchBlock *next, *prev;
};
static SketchBlock s_blocks = {&s_blocks, &s_blocks};

static void *block_link(SketchBlock *b)
{
    b->next = s_blocks.next;
    b->prev = &s_blocks;
    s_blocks.next->prev = b;
    s_blocks.next = b;
    return b + 1;
}

static SketchBlock *block_unlink(void *ptr)
{
    SketchBlock *b = (SketchBlock *)ptr - 1;
    b->prev->next = b->next;
    b->next->prev = b->prev;
    return b;
}

extern "C" void *bl_impl_malloc(size_t size)
{
    SketchBlock *b = size < SIZE_MAX - sizeof(SketchBlock) ? (SketchBlock *)malloc(size + sizeof(SketchBlock)) : NULL;
    return b ? block_link(b) : NULL;
}

extern "C" void *bl_impl_calloc(size_t n, size_t size)
{
    if (size && n > (SIZE_MAX - sizeof(SketchBlock)) / size) {
        return NULL;
    }
    void *p = bl_impl_malloc(n * size);
    if (p) {
        memset(p, 0, n * size);
    }
    return p;
}

extern "C" void *bl_impl_realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return bl_impl_malloc(size);
    }
    if (size >= SIZE_MAX - sizeof(SketchBlock)) {
        return NULL;
    }
    SketchBlock *old = block_unlink(ptr);
    SketchBlock *b = (SketchBlock *)realloc(old, size + sizeof(SketchBlock));
    if (!b) {
        block_link(old);        // realloc failed: the old block is still the sketch's
        return NULL;
    }
    return block_link(b);
}

extern "C" void bl_impl_free(void *ptr)
{
    if (ptr) {
        free(block_unlink(ptr));
    }
}

/* Frees every block the sketch still holds. */
void bl_sketch_heap_release(void)
{
    while (s_blocks.next != &s_blocks) {
        free(block_unlink(s_blocks.next + 1));
    }
}

extern "C" uint32_t bl_impl_free_heap(void)
{
    mbed_stats_heap_t st;
    mbed_stats_heap_get(&st);
    extern char __end__, __HeapLimit;
    uint32_t total = (uint32_t)(&__HeapLimit - &__end__);
    struct mallinfo mi = mallinfo();
    // Mbed's heap statistics may be compiled out: count from the heap's span then.
    return st.reserved_size ? (uint32_t)(st.reserved_size - st.current_size)
                            : total - (uint32_t)mi.arena + (uint32_t)mi.fordblks;
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
    extern char __end__, __HeapLimit;
    switch (what) {
    case BL_INFO_CPU_MHZ:
        return SystemCoreClock / 1000000;
    case BL_INFO_HEAP_SIZE:
        return (uint32_t)(&__HeapLimit - &__end__);
    case BL_INFO_MIN_FREE_HEAP:
    case BL_INFO_MAX_ALLOC_HEAP:
        return bl_impl_free_heap();
    case BL_INFO_FLASH_SIZE:
        return BL_APOLLO3_FLASH_SIZE;
    default:
        return 0;
    }
}

/* The Apollo3 has a true random number generator only in its BLE radio; until
 * that is up, a counter mixed with the ADC's noise and the clock. */
extern "C" uint32_t bl_impl_random(void)
{
    static uint32_t s = 0x9E3779B9;
    s ^= us_ticker_read() * 2654435761u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

extern "C" int bl_impl_reset_reason(void)
{
    return bl_reset_reason_code();
}

extern "C" int bl_impl_last_crash(char *buf, size_t size)
{
    return bl_last_crash_text(buf, size);
}

/* ---- sleep: the sketch waits, then restarts (bl_sleep_restart): the chip
 * idles meanwhile (Mbed sleeps between ticks) and RTC memory is kept. Wake
 * sources: the timer, and one pin at a level (EXT0). */
static uint64_t s_wake_us;
static int s_wake_pin = -1, s_wake_level;

extern "C" int bl_impl_sleep_wake(int source, uint64_t value, int pin, int level)
{
    if (source == BL_WAKE_TIMER) {
        s_wake_us = value;
        return BL_OK;
    }
    if (source == BL_WAKE_EXT0) {
        if (pin < 0 || pin >= 50 || pin == 48 || pin == 49) {
            return BL_ERR_ARG;
        }
        s_wake_pin = pin;
        s_wake_level = level ? 1 : 0;
        return BL_OK;
    }
    return BL_ERR_UNSUPPORTED;
}

extern "C" int bl_impl_sleep_disable_wake(int source)
{
    if (source == BL_WAKE_TIMER || source == BL_WAKE_ALL) {
        s_wake_us = 0;
    }
    if (source == BL_WAKE_EXT0 || source == BL_WAKE_ALL) {
        s_wake_pin = -1;
    }
    return BL_OK;
}

extern "C" void bl_impl_sleep_deep(void)
{
    bl_sleep_restart(s_wake_us, s_wake_pin, s_wake_level);
}

extern "C" int bl_impl_sleep_wake_cause(void)
{
    return bl_wake_cause_code();
}

extern "C" void *bl_impl_rtc_memory(uint32_t *size)
{
    return bl_rtc_memory_block(size);
}

/* ---- GPIO: pin numbers are Apollo3 pads (0-49), as the board's labels.
 * The console's pads (48/49) belong to Baseline. */
static bool pin_ok(int pin)
{
    return pin >= 0 && pin < 50 && pin != 48 && pin != 49 && pinIndexByNumber(pin) != variantPinCount;
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
    case BL_OUTPUT_OPEN_DRAIN: return BL_ERR_UNSUPPORTED;     // the core has no open-drain mode yet
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

/* Interrupts are deferred, as on the ESP32 family: the pin goes into a queue,
 * and the SDK calls the sketch's handler on the sketch's own thread. */
#define IRQ_QUEUE_LEN 32
static volatile uint8_t s_irq_q[IRQ_QUEUE_LEN];
static volatile uint8_t s_irq_head, s_irq_tail;

static uint64_t s_attached;     /* pins with an interrupt */

/* Every interrupt detached, the queue emptied, the wake sources cleared (a sketch restart). */
void bl_gpio_reset(void)
{
    for (int pin = 0; pin < 50; pin++) {
        if (s_attached & (1ull << pin)) {
            detachInterrupt((pin_size_t)pin);
        }
    }
    s_attached = 0;
    s_irq_head = s_irq_tail = 0;
    s_wake_us = 0;
    s_wake_pin = -1;
}

static void pin_isr(void *arg)
{
    uint8_t next = (uint8_t)((s_irq_head + 1) % IRQ_QUEUE_LEN);
    if (next != s_irq_tail) {
        s_irq_q[s_irq_head] = (uint8_t)(uintptr_t)arg;
        s_irq_head = next;
    }
}

extern "C" int bl_impl_gpio_attach_interrupt(int pin, int mode)
{
    if (!pin_ok(pin) || mode < BL_RISING || mode > BL_ONHIGH) {
        return BL_ERR_ARG;
    }
    static const PinStatus modes[] = {RISING, RISING, FALLING, CHANGE, LOW, HIGH};
    attachInterruptParam((pin_size_t)pin, pin_isr, modes[mode], (void *)(uintptr_t)pin);
    s_attached |= 1ull << pin;
    return BL_OK;
}

extern "C" int bl_impl_gpio_detach_interrupt(int pin)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    detachInterrupt((pin_size_t)pin);
    s_attached &= ~(1ull << pin);
    return BL_OK;
}

extern "C" int bl_impl_gpio_interrupt_take(void)
{
    if (s_irq_head == s_irq_tail) {
        return -1;
    }
    int pin = s_irq_q[s_irq_tail];
    s_irq_tail = (uint8_t)((s_irq_tail + 1) % IRQ_QUEUE_LEN);
    return pin;
}

/* ---- analog, PWM ---- */
static bool adc_pin(int pin)
{
    switch (pin) {
    case 11: case 12: case 13: case 16: case 29: case 31: case 32: case 33: case 34: case 35:
        return true;
    default:
        return false;
    }
}

extern "C" int bl_impl_adc_read(int pin)
{
    if (!adc_pin(pin)) {
        return BL_ERR_ARG;
    }
    static bool res_set;
    if (!res_set) {
        analogReadResolution(12);       // the API's 0-4095
        res_set = true;
    }
    return analogRead((pin_size_t)pin);
}

extern "C" int bl_impl_adc_read_mv(int pin)
{
    int v = bl_impl_adc_read(pin);
    return v < 0 ? v : (int)((uint32_t)v * 2000 / 4095);   // the Apollo3 ADC's 2.0 V reference
}

/* analogWrite(): 8 bits at 183 Hz on a pad with a CTIMER output, through
 * Baseline's per-pin PWM (bl_apollo3_hw.cpp; the core's own analogWrite() left
 * the ATP's STAT LED, pad 5, dark). On a pad without one it acts as on a
 * classic Arduino: on at 128 and above, off below. */
extern "C" int bl_impl_ledc_attach(int pin, uint32_t freq, int resolution);
extern "C" int bl_impl_ledc_write(int pin, uint32_t duty);
static uint64_t s_no_pwm;       /* pads found without a CTIMER output */

extern "C" int bl_impl_pwm_write(int pin, int value)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    value = constrain(value, 0, 255);
    if (!(s_no_pwm & (1ull << pin))) {
        if (bl_impl_ledc_write(pin, (uint32_t)value) || (bl_impl_ledc_attach(pin, 183, 8) && bl_impl_ledc_write(pin, (uint32_t)value))) {
            return BL_OK;
        }
        s_no_pwm |= 1ull << pin;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value >= 128 ? HIGH : LOW);
    return BL_OK;
}
