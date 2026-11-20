/*
 * bl_rp2_impl.cpp - the API table entries Baseline implements on the RP2350,
 * on top of arduino-pico: time, console, memory, system, GPIO (with deferred
 * interrupts), ADC, PWM, and WS2812 LEDs through a PIO state machine. Every
 * other entry is a weak stub (bl_rp2_weak.cpp), which answers like a chip
 * without the hardware (BL_ERR_UNSUPPORTED, 0, -1 or NULL).
 */
#include <malloc.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <PIOProgram.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/sio.h>
#include <hardware/sync.h>
#include "bl_rp2.h"

/* ---- time ---- */
extern "C" uint32_t bl_impl_millis(void)
{
    return millis();
}

extern "C" uint64_t bl_impl_micros(void)
{
    return time_us_64();
}

/* The date and time (UTC): set by NTP (bl_rp2_net.cpp) or by the sketch. */
extern "C" int bl_impl_time_get(int64_t *sec, int32_t *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (sec) {
        *sec = tv.tv_sec;
    }
    if (usec) {
        *usec = (int32_t)tv.tv_usec;
    }
    return BL_OK;
}

extern "C" int bl_impl_time_set(int64_t sec, int32_t usec)
{
    struct timeval tv = { (time_t)sec, (suseconds_t)usec };
    return settimeofday(&tv, NULL) == 0 ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_time_synced(void)
{
    return time(NULL) > 1609459200;     // after 2021-01-01: the clock has been set
}

extern "C" void bl_impl_delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    do {
        bl_poll();
        tight_loop_contents();
    } while (millis() - start < ms);
}

extern "C" void bl_impl_delay_us(uint32_t us)
{
    delayMicroseconds(us);
}

extern "C" void bl_impl_yield(void)
{
    bl_poll();
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

extern "C" int bl_impl_vsscanf(const char *str, const char *fmt, va_list ap)
{
    return vsscanf(str, fmt, ap);
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

/* ---- memory: arduino-pico's heap; PSRAM through its pmalloc pool ----
 * A sketch's blocks over 4 KB go to PSRAM when the board has it, as on the
 * ESP32 family (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096); internal RAM if
 * PSRAM is full. Baseline's own allocations stay in internal RAM. */
#define PSRAM_ABOVE 4096

extern "C" void *bl_impl_malloc(size_t size)
{
#ifdef RP2350_PSRAM_CS
    if (size > PSRAM_ABOVE) {
        void *p = pmalloc(size);
        if (p) {
            return p;
        }
    }
#endif
    return malloc(size);
}

extern "C" void *bl_impl_calloc(size_t n, size_t size)
{
#ifdef RP2350_PSRAM_CS
    if (size && n > PSRAM_ABOVE / size) {
        void *p = pcalloc(n, size);
        if (p) {
            return p;
        }
    }
#endif
    return calloc(n, size);
}

/* arduino-pico's realloc() and free() recognize PSRAM blocks themselves. A
 * block that starts big, or outgrows internal RAM, goes to PSRAM as malloc()'s
 * would (Arduino Strings grow with realloc: a 600 KB HTTP body came back empty). */
extern "C" void *bl_impl_realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return bl_impl_malloc(size);
    }
    void *p = realloc(ptr, size);
#ifdef RP2350_PSRAM_CS
    if (!p && size > PSRAM_ABOVE) {
        p = pmalloc(size);
        if (p) {
            size_t old = malloc_usable_size(ptr);
            memcpy(p, ptr, old < size ? old : size);
            free(ptr);
        }
    }
#endif
    return p;
}

extern "C" void bl_impl_free(void *ptr)
{
    free(ptr);
}

extern "C" uint32_t bl_impl_free_heap(void)
{
    return (uint32_t)rp2040.getFreeHeap();
}

#ifdef RP2350_PSRAM_CS
extern "C" void *bl_impl_ps_malloc(size_t size)
{
    return pmalloc(size);
}

extern "C" void *bl_impl_ps_calloc(size_t n, size_t size)
{
    return pcalloc(n, size);
}

extern "C" void *bl_impl_ps_realloc(void *ptr, size_t size)
{
    return ptr ? realloc(ptr, size) : pmalloc(size);
}
#endif

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
        return clock_get_hz(clk_sys) / 1000000;
    case BL_INFO_HEAP_SIZE:
        return (uint32_t)rp2040.getTotalHeap();
    case BL_INFO_MIN_FREE_HEAP:
    case BL_INFO_MAX_ALLOC_HEAP:
        return bl_impl_free_heap();
#ifdef RP2350_PSRAM_CS
    case BL_INFO_PSRAM_SIZE:
        return (uint32_t)rp2040.getTotalPSRAMHeap();
    case BL_INFO_FREE_PSRAM:
        return (uint32_t)rp2040.getFreePSRAMHeap();
#endif
    case BL_INFO_FLASH_SIZE:
        return BL_BOARD_FLASH_SIZE;
    default:
        return 0;
    }
}

extern "C" float bl_impl_temperature_read(void)
{
    return analogReadTemp();
}

extern "C" uint32_t bl_impl_random(void)
{
    return rp2040.hwrand32();
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

/* ---- GPIO ----
 * Pins 0 to __GPIOCNT-1 are the chip's; on boards with a CYW43 radio,
 * arduino-pico's pins 64+ are the radio's GPIOs (the STAT LED on 64). */
/* BOOTSEL (BL_TARGET_BOOT_PIN): the button pulls the flash chip select low.
 * Reading it lets go of the chip select for a moment, so it runs from RAM with
 * interrupts off and the other core idle (arduino-pico's BOOTSEL does the same;
 * the boot ROM reads it at reset). LOW while pressed, like a button to ground. */
static bool __no_inline_not_in_flash_func(bootsel_pressed)(void)
{
    noInterrupts();
    rp2040.idleOtherCore();
    hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; i++) {
    }
#if PICO_RP2040
    bool pressed = !(sio_hw->gpio_hi_in & (1u << 1));
#else
    bool pressed = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#endif
    hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    rp2040.resumeOtherCore();
    interrupts();
    return pressed;
}

static int8_t s_bootsel_irq = -1;       /* attachInterrupt mode on BOOTSEL, -1: none */
static int8_t s_bootsel_level = -1;     /* last level seen by the poll */
static uint32_t s_bootsel_ms;

static void bootsel_queue(void);

/* From bl_poll(): BOOTSEL has no interrupt of its own, so it is read every
 * 20 ms while an interrupt is attached to it. */
void bl_bootsel_poll(void)
{
    if (s_bootsel_irq < 0 || millis() - s_bootsel_ms < 20) {
        return;
    }
    s_bootsel_ms = millis();
    int level = bootsel_pressed() ? 0 : 1;
    int was = s_bootsel_level;
    s_bootsel_level = (int8_t)level;
    bool fire = false;
    switch (s_bootsel_irq) {
    case BL_RISING:  fire = was == 0 && level == 1; break;
    case BL_FALLING: fire = was == 1 && level == 0; break;
    case BL_CHANGE:  fire = was >= 0 && was != level; break;
    case BL_ONLOW:   fire = level == 0; break;
    case BL_ONHIGH:  fire = level == 1; break;
    }
    if (fire) {
        bootsel_queue();
    }
}

static bool pin_ok(int pin)
{
    if (pin == BL_TARGET_BOOT_PIN) {
        return true;
    }
#ifdef PICO_CYW43_SUPPORTED
    if (pin >= 64 && pin <= 66) {
        return true;
    }
#endif
    return pin >= 0 && pin < __GPIOCNT;
}

static int8_t s_ws2812_pin = -1;      /* the pin the PIO LED driver owns */

static void release_ws2812(int pin);

extern "C" int bl_impl_gpio_mode(int pin, int mode)
{
    if (!pin_ok(pin)) {
        return BL_ERR_ARG;
    }
    if (pin == BL_TARGET_BOOT_PIN) {
        return mode == BL_OUTPUT || mode == BL_OUTPUT_OPEN_DRAIN ? BL_ERR_ARG : BL_OK;     /* a button: input only */
    }
    release_ws2812(pin);
    switch (mode) {
    case BL_INPUT:            pinMode(pin, INPUT); break;
    case BL_OUTPUT:           pinMode(pin, OUTPUT); break;
    case BL_INPUT_PULLUP:     pinMode(pin, INPUT_PULLUP); break;
    case BL_INPUT_PULLDOWN:   pinMode(pin, INPUT_PULLDOWN); break;
    case BL_OUTPUT_OPEN_DRAIN: pinMode(pin, OUTPUT_2MA); break;    // no open drain on RP2: a weak push-pull output
    default:                  return BL_ERR_ARG;
    }
    return BL_OK;
}

extern "C" int bl_impl_gpio_write(int pin, int level)
{
    if (!pin_ok(pin) || pin == BL_TARGET_BOOT_PIN) {
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
    if (pin == BL_TARGET_BOOT_PIN) {
        return bootsel_pressed() ? 0 : BL_HIGH;
    }
    return digitalRead(pin) ? BL_HIGH : 0;
}

/* Interrupts are deferred, as on the ESP32 family: the pin goes into a queue,
 * and the SDK calls the sketch's handler on the sketch's own context. */
#define IRQ_QUEUE_LEN 32
static volatile uint8_t s_irq_q[IRQ_QUEUE_LEN];
static volatile uint8_t s_irq_head, s_irq_tail;

static void pin_isr(void *arg)
{
    uint8_t next = (uint8_t)((s_irq_head + 1) % IRQ_QUEUE_LEN);
    if (next != s_irq_tail) {
        s_irq_q[s_irq_head] = (uint8_t)(uintptr_t)arg;
        s_irq_head = next;
    }
}

static void bootsel_queue(void)
{
    pin_isr((void *)(uintptr_t)BL_TARGET_BOOT_PIN);
}

extern "C" int bl_impl_gpio_attach_interrupt(int pin, int mode)
{
    if (pin == BL_TARGET_BOOT_PIN && mode >= BL_RISING && mode <= BL_ONHIGH) {
        s_bootsel_level = -1;
        s_bootsel_irq = (int8_t)mode;
        return BL_OK;
    }
    if (pin < 0 || pin >= __GPIOCNT || mode < BL_RISING || mode > BL_ONHIGH) {
        return BL_ERR_ARG;
    }
    static const PinStatus modes[] = {RISING, RISING, FALLING, CHANGE, LOW, HIGH};
    attachInterruptParam(pin, pin_isr, modes[mode], (void *)(uintptr_t)pin);
    return BL_OK;
}

extern "C" int bl_impl_gpio_detach_interrupt(int pin)
{
    if (pin == BL_TARGET_BOOT_PIN) {
        s_bootsel_irq = -1;
        return BL_OK;
    }
    if (pin < 0 || pin >= __GPIOCNT) {
        return BL_ERR_ARG;
    }
    detachInterrupt(pin);
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
    return pin >= __FIRSTANALOGGPIO && pin < __FIRSTANALOGGPIO + 4 + (__GPIOCNT == 48 ? 4 : 0);
}

extern "C" int bl_impl_adc_read(int pin)
{
    if (!adc_pin(pin)) {
        return BL_ERR_ARG;
    }
    release_ws2812(pin);
    return analogRead(pin);     // 12 bits (bl_rp2_start sets the resolution)
}

extern "C" int bl_impl_adc_read_mv(int pin)
{
    int v = bl_impl_adc_read(pin);
    return v < 0 ? v : (int)((uint32_t)v * 3300 / 4095);
}

extern "C" int bl_impl_pwm_write(int pin, int value)
{
    if (pin < 0 || pin >= __GPIOCNT) {
        return BL_ERR_ARG;
    }
    release_ws2812(pin);
    analogWrite(pin, constrain(value, 0, 255));
    return BL_OK;
}

/* ---- WS2812 / SK6812 / WS2811 LEDs: one PIO state machine, on the pin last
 * written. The program is pico-examples' ws2812.pio (T1 2, T2 5, T3 3 cycles,
 * side-set on the data pin). */
static const uint16_t k_ws2812_insns[] = {
    0x6221,     //  0: out    x, 1            side 0 [2]
    0x1123,     //  1: jmp    !x, 3           side 1 [1]
    0x1400,     //  2: jmp    0               side 1 [4]
    0xa442,     //  3: nop                    side 0 [4]
};
static pio_program_t ws2812_program(void)
{
    pio_program_t p = {};
    p.instructions = k_ws2812_insns;
    p.length = 4;
    p.origin = -1;
    return p;
}
static const pio_program_t k_ws2812_program = ws2812_program();
static PIOProgram s_ws2812(&k_ws2812_program);
static PIO s_pio;
static int s_sm = -1, s_offset;
static uint32_t s_ws2812_hz;

static void release_ws2812(int pin)
{
    if (pin == s_ws2812_pin && s_sm >= 0) {
        pio_sm_set_enabled(s_pio, s_sm, false);
        s_ws2812_pin = -1;
    }
}

static bool ws2812_on(int pin, uint32_t hz)
{
    if (s_sm < 0 && !s_ws2812.prepare(&s_pio, &s_sm, &s_offset, pin, 1)) {
        s_sm = -1;
        return false;
    }
    if (pin == s_ws2812_pin && hz == s_ws2812_hz) {
        return true;
    }
    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_gpio_init(s_pio, pin);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, pin, 1, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, s_offset, s_offset + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (hz * 10.0f));
    pio_sm_init(s_pio, s_sm, s_offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);
    s_ws2812_pin = (int8_t)pin;
    s_ws2812_hz = hz;
    return true;
}

static void ws2812_put(uint32_t grb)
{
    pio_sm_put_blocking(s_pio, s_sm, grb << 8);
}

static void ws2812_latch(void)
{
    uint32_t t = micros();
    while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm) && micros() - t < 10000) {
    }
    delayMicroseconds(60);      // the last bits leave the shift register, then the reset gap
}

extern "C" int bl_impl_rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)
{
    if (pin < 0 || pin >= __GPIOCNT) {
        return BL_ERR_ARG;
    }
    if (!ws2812_on(pin, 800000)) {
        return BL_ERR_FAIL;
    }
    ws2812_put((uint32_t)green << 16 | (uint32_t)red << 8 | blue);
    ws2812_latch();
    return BL_OK;
}

/* data: 3 bytes (GRB order, as the LEDs take them) per LED, or 4 for RGBW. */
extern "C" int bl_impl_led_strip_write(int pin, int chipset, const uint8_t *data, size_t len)
{
    if (pin < 0 || pin >= __GPIOCNT || chipset < 0 || chipset > BL_STRIP_WS2811 || (!data && len)) {
        return BL_ERR_ARG;
    }
    if (!ws2812_on(pin, chipset == BL_STRIP_WS2811 ? 400000 : 800000)) {
        return BL_ERR_FAIL;
    }
    // 8 bits at a time: the shift register sends whatever length the strip has.
    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, s_offset, s_offset + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (s_ws2812_hz * 10.0f));
    pio_sm_init(s_pio, s_sm, s_offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(s_pio, s_sm, (uint32_t)data[i] << 24);
    }
    ws2812_latch();
    s_ws2812_hz = 0;        // rgb_write sets the state machine up for 24 bits again
    return BL_OK;
}
