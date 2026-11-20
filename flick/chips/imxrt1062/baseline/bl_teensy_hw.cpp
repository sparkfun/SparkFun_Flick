/*
 * bl_teensy_hw.cpp - UARTs, SPI, I2C buses, LEDC-style PWM, PSRAM, timers and
 * the hardware random number generator on the i.MX RT1062 (Teensyduino).
 */
#include <SPI.h>
#include <Wire.h>
#include "bl_teensy.h"

extern "C" {
extern uint8_t external_psram_size;     /* startup.c: MB of PSRAM found (Teensy 4.1) */
void *extmem_malloc(size_t size);
void extmem_free(void *ptr);
void *extmem_calloc(size_t nmemb, size_t size);
void *extmem_realloc(void *ptr, size_t size);
}

/* ---------------- UARTs: ports 1-7 (Teensy 4.0) or 1-8 (4.1) are Serial1..Serial8 ---------------- */

static HardwareSerialIMXRT *uart(int port)
{
    switch (port) {
    case 1: return &Serial1;
    case 2: return &Serial2;
    case 3: return &Serial3;
    case 4: return &Serial4;
    case 5: return &Serial5;
    case 6: return &Serial6;
    case 7: return &Serial7;
#if defined(ARDUINO_TEENSY41)
    case 8: return &Serial8;
#endif
    default: return NULL;
    }
}

static size_t s_rx_extra[9];    /* uart_set_rx_buffer(), added at begin */

/* The SDK's SERIAL_* values are Arduino-ESP32's (parity in bits 0-1, data bits
 * in 2-3, stop bits in 4-5). Teensy has its own, and no 5/6/7N formats. */
static int teensy_format(uint32_t config)
{
    int parity = config & 3, bits = (config >> 2) & 3, stop = (config >> 4) & 3;
    int f;
    if (bits == 3) {
        f = parity == 2 ? SERIAL_8E1 : parity == 3 ? SERIAL_8O1 : SERIAL_8N1;
    } else if (bits == 2 && parity) {
        f = parity == 2 ? SERIAL_7E1 : SERIAL_7O1;
    } else {
        return -1;
    }
    if (stop == 3) {
        f |= SERIAL_2STOP_BITS;
    }
    return f;
}

static int s_format[9];   /* each port's format, for a change of baud rate */

extern "C" int bl_impl_uart_begin(int port, uint32_t baud, uint32_t config, int rx, int tx)
{
    HardwareSerialIMXRT *u = uart(port);
    int format = teensy_format(config);
    if (!u || format < 0 || !baud) {
        return u ? BL_ERR_ARG : BL_ERR_UNSUPPORTED;
    }
    if (rx >= 0) {
        u->setRX(rx);
    }
    if (tx >= 0) {
        u->setTX(tx);
    }
    if (s_rx_extra[port]) {
        void *buf = malloc(s_rx_extra[port]);
        if (buf) {
            u->addMemoryForRead(buf, s_rx_extra[port]);
        }
        s_rx_extra[port] = 0;
    }
    u->begin(baud, format);
    s_format[port] = format;
    return BL_OK;
}

/* updateBaudRate (ABI 1.10): begin() again with the same format and pins. */
extern "C" int bl_impl_uart_set_baud(int port, uint32_t baud)
{
    HardwareSerialIMXRT *u = uart(port);
    if (!u) {
        return BL_ERR_UNSUPPORTED;
    }
    if (!baud) {
        return BL_ERR_ARG;
    }
    u->begin(baud, s_format[port]);
    return BL_OK;
}

extern "C" void bl_impl_uart_end(int port)
{
    HardwareSerialIMXRT *u = uart(port);
    if (u) {
        u->end();
    }
}

extern "C" int bl_impl_uart_write(int port, const uint8_t *data, size_t len)
{
    HardwareSerialIMXRT *u = uart(port);
    return (u && data) ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_uart_available(int port)
{
    HardwareSerialIMXRT *u = uart(port);
    return u ? u->available() : 0;
}

extern "C" int bl_impl_uart_read(int port, uint8_t *buf, size_t size)
{
    HardwareSerialIMXRT *u = uart(port);
    if (!u || !buf) {
        return 0;
    }
    size_t n = 0;
    while (n < size && u->available() > 0) {
        buf[n++] = (uint8_t)u->read();
    }
    return (int)n;
}

extern "C" int bl_impl_uart_peek(int port)
{
    HardwareSerialIMXRT *u = uart(port);
    return u ? u->peek() : -1;
}

extern "C" void bl_impl_uart_flush(int port)
{
    HardwareSerialIMXRT *u = uart(port);
    if (u) {
        u->flush();
    }
}

extern "C" int bl_impl_uart_set_rx_buffer(int port, size_t size)
{
    if (!uart(port)) {
        return BL_ERR_UNSUPPORTED;
    }
    s_rx_extra[port] = size > 64 ? size - 64 : 0;   // on top of Teensy's own 64 bytes
    return BL_OK;
}

extern "C" int bl_impl_uart_available_for_write(int port)
{
    HardwareSerialIMXRT *u = uart(port);
    return u ? u->availableForWrite() : 0;
}

/* ---------------- SPI (pins 11/12/13) ---------------- */

extern "C" int bl_impl_spi_begin(int sck, int miso, int mosi, int ss)
{
    if (sck >= 0 && sck != BL_BOARD_SCK) {
        SPI.setSCK(sck);
    }
    if (miso >= 0 && miso != BL_BOARD_MISO) {
        SPI.setMISO(miso);
    }
    if (mosi >= 0 && mosi != BL_BOARD_MOSI) {
        SPI.setMOSI(mosi);
    }
    SPI.begin();
    return BL_OK;
}

extern "C" void bl_impl_spi_end(void)
{
    SPI.end();
}

extern "C" void bl_impl_spi_begin_transaction(uint32_t clock, int bit_order, int data_mode)
{
    static const uint8_t modes[4] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    SPI.beginTransaction(SPISettings(clock, bit_order == BL_LSBFIRST ? LSBFIRST : MSBFIRST, modes[data_mode & 3]));
}

extern "C" void bl_impl_spi_end_transaction(void)
{
    SPI.endTransaction();
}

extern "C" void bl_impl_spi_transfer(uint8_t *data, size_t len)
{
    if (data && len) {
        SPI.transfer(data, len);
    }
}

extern "C" void bl_impl_spi_write(const uint8_t *data, size_t len)
{
    if (data && len) {
        SPI.transfer(data, NULL, len);
    }
}

/* ---------------- I2C: bus 0 = Wire (18/19), 1 = Wire1 (17/16), 2 = Wire2 (25/24) ---------------- */

static TwoWire *wire(int bus)
{
    return bus == 0 ? &Wire : bus == 1 ? &Wire1 : bus == 2 ? &Wire2 : NULL;
}

extern "C" int bl_impl_i2cx_begin(int bus, int sda, int scl, uint32_t freq)
{
    TwoWire *w = wire(bus);
    if (!w) {
        return BL_ERR_UNSUPPORTED;
    }
    if (sda >= 0) {
        w->setSDA(sda);
    }
    if (scl >= 0) {
        w->setSCL(scl);
    }
    w->begin();
    w->setClock(freq ? freq : 100000);
    return BL_OK;
}

extern "C" void bl_impl_i2cx_end(int bus)
{
    TwoWire *w = wire(bus);
    if (w) {
        w->end();
    }
}

extern "C" int bl_impl_i2cx_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    TwoWire *w = wire(bus);
    if (!w) {
        return BL_ERR_UNSUPPORTED;
    }
    w->beginTransmission(addr);
    if (len) {
        w->write(data, len);
    }
    switch (w->endTransmission(stop)) {
    case 0:  return BL_OK;
    case 2:
    case 3:  return BL_ERR_NACK;
    case 5:  return BL_ERR_TIMEOUT;
    default: return BL_ERR_FAIL;
    }
}

extern "C" int bl_impl_i2cx_read(int bus, uint8_t addr, uint8_t *data, size_t len)
{
    TwoWire *w = wire(bus);
    if (!w) {
        return BL_ERR_UNSUPPORTED;
    }
    size_t n = w->requestFrom(addr, len);
    if (n == 0) {
        return BL_ERR_NACK;
    }
    for (size_t i = 0; i < n; i++) {
        data[i] = (uint8_t)w->read();
    }
    return (int)n;
}

extern "C" int bl_impl_i2c_begin(int sda, int scl, uint32_t freq)
{
    return bl_impl_i2cx_begin(0, sda == BL_BOARD_SDA ? -1 : sda, scl == BL_BOARD_SCL ? -1 : scl, freq);
}

extern "C" int bl_impl_i2c_write(uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    return bl_impl_i2cx_write(0, addr, data, len, stop);
}

extern "C" int bl_impl_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    return bl_impl_i2cx_read(0, addr, data, len);
}

/* ---------------- LEDC-style PWM (the SDK's Servo, tone() and ledc*()) ----------------
 * Teensy's analogWrite resolution is global: it is set for each write. */

static uint8_t s_ledc_bits[CORE_NUM_DIGITAL];

extern "C" int bl_impl_ledc_attach(int pin, uint32_t freq, int resolution)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !digitalPinHasPWM(pin) || resolution < 1 || resolution > 16) {
        return 0;
    }
    s_ledc_bits[pin] = (uint8_t)resolution;
    analogWriteFrequency(pin, freq);
    return 1;
}

extern "C" int bl_impl_ledc_write(int pin, uint32_t duty)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !s_ledc_bits[pin]) {
        return 0;
    }
    analogWriteResolution(s_ledc_bits[pin]);
    analogWrite(pin, duty);
    analogWriteResolution(8);       // analogWrite()'s 0-255 for everyone else
    return 1;
}

extern "C" int bl_impl_ledc_detach(int pin)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !s_ledc_bits[pin]) {
        return 0;
    }
    analogWrite(pin, 0);
    pinMode(pin, INPUT);
    s_ledc_bits[pin] = 0;
    return 1;
}

extern "C" uint32_t bl_impl_ledc_write_tone(int pin, uint32_t freq)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !digitalPinHasPWM(pin)) {
        return 0;
    }
    if (!freq) {
        analogWrite(pin, 0);
        return 0;
    }
    analogWriteFrequency(pin, freq);
    analogWrite(pin, 128);      // a square wave
    return freq;
}

extern "C" uint32_t bl_impl_ledc_change_frequency(int pin, uint32_t freq, int resolution)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !s_ledc_bits[pin]) {
        return 0;
    }
    if (resolution >= 1 && resolution <= 16) {
        s_ledc_bits[pin] = (uint8_t)resolution;
    }
    analogWriteFrequency(pin, freq);
    return freq;
}

/* ---------------- PSRAM (Teensy 4.1 with chips on its pads) ---------------- */

bool bl_is_psram(const void *p)
{
    return (uint32_t)p >= 0x70000000 && (uint32_t)p < 0x72000000;
}

extern "C" void *bl_impl_ps_malloc(size_t size)
{
    return external_psram_size ? extmem_malloc(size) : NULL;    // extmem_malloc() alone falls back to RAM
}

extern "C" void *bl_impl_ps_calloc(size_t n, size_t size)
{
    return external_psram_size ? extmem_calloc(n, size) : NULL;
}

extern "C" void *bl_impl_ps_realloc(void *ptr, size_t size)
{
    return external_psram_size ? extmem_realloc(ptr, size) : NULL;
}

/* ---------------- timers (Ticker, hw_timer, esp_timer): fired from bl_poll() ----------------
 * The sketch's callbacks run on its own context, like on the ESP32 family;
 * IntervalTimer is the one for interrupt context. */

#define MAX_TIMERS 16
#define FIRED_SIZE 32

struct soft_timer {
    bool used, active, periodic;
    uint64_t period, next;
};
static soft_timer s_timers[MAX_TIMERS];
static int8_t s_fired[FIRED_SIZE];
static uint8_t s_fired_head, s_fired_tail;

void bl_timers_poll(void)
{
    uint64_t now = bl_impl_micros();
    for (int h = 0; h < MAX_TIMERS; h++) {
        soft_timer *t = &s_timers[h];
        if (!t->active || (int64_t)(now - t->next) < 0) {
            continue;
        }
        uint8_t next = (s_fired_head + 1) % FIRED_SIZE;
        if (next != s_fired_tail) {
            s_fired[s_fired_head] = (int8_t)h;
            s_fired_head = next;
        }
        if (t->periodic) {
            t->next += t->period;
            if ((int64_t)(now - t->next) > 0) {
                t->next = now + t->period;     // fell behind: don't fire a burst
            }
        } else {
            t->active = false;
        }
    }
}

extern "C" int bl_impl_timer_new(void)
{
    for (int h = 0; h < MAX_TIMERS; h++) {
        if (!s_timers[h].used) {
            s_timers[h] = {};
            s_timers[h].used = true;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_timer_free(int h)
{
    if (h >= 0 && h < MAX_TIMERS) {
        s_timers[h] = {};
    }
}

extern "C" int bl_impl_timer_start(int h, uint64_t period_us, int periodic)
{
    if (h < 0 || h >= MAX_TIMERS || !s_timers[h].used || !period_us) {
        return BL_ERR_ARG;
    }
    soft_timer *t = &s_timers[h];
    t->period = period_us;
    t->periodic = periodic != 0;
    t->next = bl_impl_micros() + period_us;
    t->active = true;
    return BL_OK;
}

extern "C" void bl_impl_timer_stop(int h)
{
    if (h >= 0 && h < MAX_TIMERS) {
        s_timers[h].active = false;
    }
}

extern "C" int bl_impl_timer_active(int h)
{
    return (h >= 0 && h < MAX_TIMERS && s_timers[h].active) ? 1 : 0;
}

extern "C" int bl_impl_timer_take(void)
{
    bl_poll();
    if (s_fired_head == s_fired_tail) {
        return -1;
    }
    int h = s_fired[s_fired_tail];
    s_fired_tail = (s_fired_tail + 1) % FIRED_SIZE;
    return h;
}

/* ---------------- hardware random numbers (the i.MX RT TRNG) ---------------- */

/* The TRNG runs with its reset-default sampling and statistical-check settings.
 * One generation fills ENT0..ENT15 (512 bits); reading ENT15 starts the next.
 * A failed statistical check sets ERR: the TRNG is reset and started again. */
static void trng_start(void)
{
    CCM_CCGR6 |= CCM_CCGR6_TRNG(CCM_CCGR_ON);
    TRNG_MCTL = TRNG_MCTL_PRGM | TRNG_MCTL_RST_DEF;     // program mode, default settings
    TRNG_MCTL &= ~TRNG_MCTL_PRGM;                       // run mode: generation starts
}

extern "C" uint32_t bl_impl_random(void)
{
    static uint32_t pool[16];
    static uint8_t left;        // words of pool not yet handed out
    static bool started;
    if (!started) {
        trng_start();
        started = true;
    }
    if (left == 0) {
        uint32_t t0 = millis();
        for (;;) {
            uint32_t mctl = TRNG_MCTL;
            if (mctl & TRNG_MCTL_ERR) {
                trng_start();
            } else if (mctl & TRNG_MCTL_ENT_VAL) {
                break;
            }
            if (millis() - t0 > 200) {
                // The TRNG isn't producing: don't hang the caller (weak, but never stuck).
                return ARM_DWT_CYCCNT ^ (micros() << 16);
            }
        }
        volatile uint32_t *ent = &TRNG_ENT0;
        for (int i = 0; i < 16; i++) {
            pool[i] = ent[i];   // ENT15 last: that starts the next generation
        }
        left = 16;
    }
    return pool[--left];
}

/* ---------------- WS2812-type LEDs (ABI 1.1 rgb_write, 1.6 led_strip_write) ----------------
 * Bit-banged on any pin, timed with the cycle counter, interrupts off for the
 * length of a frame (about 30 us per LED; WS2812 latches if a gap reaches
 * ~50 us, so the frame can't be split). Bytes go out as given: the SDK puts them
 * in the strip's color order. */
static void strip_send(int pin, const uint8_t *data, size_t len, uint32_t t0h_ns, uint32_t t1h_ns, uint32_t bit_ns)
{
    volatile uint32_t *set = portSetRegister(pin);
    volatile uint32_t *clr = portClearRegister(pin);
    const uint32_t mask = digitalPinToBitMask(pin);
    const uint32_t mhz = F_CPU_ACTUAL / 1000000;
    const uint32_t c0 = t0h_ns * mhz / 1000, c1 = t1h_ns * mhz / 1000, cbit = bit_ns * mhz / 1000;
    ARM_DEMCR |= ARM_DEMCR_TRCENA;
    ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
    pinMode(pin, OUTPUT);
    *clr = mask;
    delayMicroseconds(60);      // reset/latch before the frame
    __disable_irq();
    uint32_t start = ARM_DWT_CYCCNT - cbit;
    for (size_t i = 0; i < len; i++) {
        for (uint8_t bit = 0x80; bit; bit >>= 1) {
            while (ARM_DWT_CYCCNT - start < cbit) {
            }
            start = ARM_DWT_CYCCNT;
            *set = mask;
            const uint32_t high = (data[i] & bit) ? c1 : c0;
            while (ARM_DWT_CYCCNT - start < high) {
            }
            *clr = mask;
        }
    }
    while (ARM_DWT_CYCCNT - start < cbit) {
    }
    __enable_irq();
    delayMicroseconds(60);      // latch
}

extern "C" int bl_impl_led_strip_write(int pin, int chipset, const uint8_t *data, size_t len)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || (!data && len)) {
        return BL_ERR_ARG;
    }
    switch (chipset) {
    case BL_STRIP_WS2812: strip_send(pin, data, len, 400, 800, 1250); break;
    case BL_STRIP_SK6812: strip_send(pin, data, len, 300, 600, 1250); break;
    case BL_STRIP_WS2811: strip_send(pin, data, len, 500, 1200, 2500); break;
    default:              return BL_ERR_ARG;
    }
    return BL_OK;
}

extern "C" int bl_impl_rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)
{
    const uint8_t grb[3] = {green, red, blue};      // a WS2812's own order
    return bl_impl_led_strip_write(pin, BL_STRIP_WS2812, grb, 3);
}
