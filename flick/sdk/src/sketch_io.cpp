/* sketch_io.cpp - attachInterrupt, SPI and hardware UARTs (see baseline_io.h). */
#include "baseline.h"

SPIClass SPI;
HardwareSerial Serial1(1);
HardwareSerial Serial2(2);

int HardwareSerial::read()
{
    uint8_t b;
    return (started_ && bl_uart_read(port_, &b, 1) == 1) ? b : -1;
}

/* ---- interrupts ---- */

#define MAX_PINS 49

struct PinHandler {
    void (*plain)(void);
    void (*with_arg)(void *);
    void *arg;
};
static PinHandler s_handlers[MAX_PINS];

#ifdef BL_TARGET_SKETCH_ISR
/* Handlers run in interrupt context: Baseline calls fn(arg) straight from the pin's ISR. */
static void call_plain(void *fn)
{
    ((void (*)(void))fn)();
}

static void attach(uint8_t pin, PinHandler h, int mode)
{
    if (h.plain) {
        bl_isr_attach_pin(pin, mode, call_plain, (void *)h.plain);
    } else {
        bl_isr_attach_pin(pin, mode, h.with_arg, h.arg);
    }
}
#else
/* Runs on the sketch task: call the handler for every pin that fired. */
static void interrupt_pump()
{
    int pin;
    while ((pin = bl_gpio_interrupt_take()) >= 0) {
        if (pin >= MAX_PINS) {
            continue;
        }
        PinHandler h = s_handlers[pin];
        if (h.plain) {
            h.plain();
        } else if (h.with_arg) {
            h.with_arg(h.arg);
        }
    }
}

static void attach(uint8_t pin, PinHandler h, int mode)
{
    if (pin >= MAX_PINS) {
        return;
    }
    s_handlers[pin] = h;
    bl_sketch_add_pump(interrupt_pump);
    bl_gpio_attach_interrupt(pin, mode);
}
#endif

void attachInterrupt(uint8_t pin, void (*handler)(void), int mode)
{
    attach(pin, PinHandler{ handler, 0, 0 }, mode);
}

void attachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, int mode)
{
    attach(pin, PinHandler{ 0, handler, arg }, mode);
}

void detachInterrupt(uint8_t pin)
{
#ifdef BL_TARGET_SKETCH_ISR
    bl_isr_detach_pin(pin);
    return;
#endif
    bl_gpio_detach_interrupt(pin);
    if (pin < MAX_PINS) {
        s_handlers[pin] = PinHandler{};
    }
}

#if defined(BL_HAS_TOUCH)
/* ---- touch interrupts (deferred, like attachInterrupt) ---- */

static PinHandler s_touch[MAX_PINS];

static void touch_pump()
{
    int pin;
    while ((pin = bl_touch_take()) >= 0) {
        if (pin >= MAX_PINS) {
            continue;
        }
        PinHandler h = s_touch[pin];
        if (h.plain) {
            h.plain();
        } else if (h.with_arg) {
            h.with_arg(h.arg);
        }
    }
}

static void touch_attach(uint8_t pin, PinHandler h, touch_value_t threshold)
{
    if (pin >= MAX_PINS) {
        return;
    }
    s_touch[pin] = h;
    bl_sketch_add_pump(touch_pump);
    bl_touch_attach(pin, threshold);
}

void touchAttachInterrupt(uint8_t pin, void (*handler)(void), touch_value_t threshold)
{
    touch_attach(pin, PinHandler{ handler, 0, 0 }, threshold);
}

void touchAttachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, touch_value_t threshold)
{
    touch_attach(pin, PinHandler{ 0, handler, arg }, threshold);
}

void touchDetachInterrupt(uint8_t pin)
{
    bl_touch_detach(pin);
    if (pin < MAX_PINS) {
        s_touch[pin] = PinHandler{};
    }
}

#endif /* BL_HAS_TOUCH */
/* ---- Servo ---- */

int Servo::attach(int pin, int min_us, int max_us)
{
    detach();
    if (!bl_ledc_attach(pin, hz_, 14)) {
        return 0;
    }
    pin_ = pin;
    min_ = min_us;
    max_ = max_us;
    writeMicroseconds(us_);
    return 1;
}

void Servo::detach()
{
    if (pin_ >= 0) {
        bl_ledc_detach(pin_);
        pin_ = -1;
    }
}

void Servo::write(int value)
{
    if (value < 500) {     // degrees
        value = constrain(value, 0, 180);
        value = map(value, 0, 180, min_, max_);
    }
    writeMicroseconds(value);
}

void Servo::writeMicroseconds(int us)
{
    us_ = constrain(us, min_, max_);
    if (pin_ >= 0) {
        uint32_t period_us = 1000000UL / hz_;
        bl_ledc_write(pin_, (uint32_t)((uint64_t)us_ * 16383 / period_us));
    }
}

#if defined(BL_PLATFORM_ESP32)
/* ---- I2S ---- */

size_t I2SClass::readBytes(char *buffer, size_t size)
{
    size_t n = 0;
    while (n < size && rxPos_ < rxLen_) {   // buffered bytes first
        buffer[n++] = (char)rx_[rxPos_++];
    }
    if (n < size) {
        int r = bl_i2s_read((uint8_t *)buffer + n, size - n);
        if (r > 0) {
            n += r;
        }
    }
    return n;
}

int I2SClass::available()
{
    if (rxPos_ >= rxLen_) {
        int r = bl_i2s_read(rx_, sizeof(rx_));
        rxLen_ = r > 0 ? r : 0;
        rxPos_ = 0;
    }
    return rxLen_ - rxPos_;
}

int I2SClass::read()
{
    return available() ? rx_[rxPos_++] : -1;
}

#endif /* BL_PLATFORM_ESP32 */
/* ---- SPI ---- */

uint16_t SPIClass::transfer16(uint16_t data)
{
    uint8_t b[2];
    bool msb = settings_.bitOrder_ == MSBFIRST;
    b[0] = msb ? data >> 8 : data;
    b[1] = msb ? data : data >> 8;
    bl_spi_transfer(b, 2);
    return msb ? (uint16_t)(b[0] << 8 | b[1]) : (uint16_t)(b[1] << 8 | b[0]);
}

uint32_t SPIClass::transfer32(uint32_t data)
{
    uint8_t b[4];
    bool msb = settings_.bitOrder_ == MSBFIRST;
    for (int i = 0; i < 4; i++) {
        b[i] = (uint8_t)(msb ? data >> (24 - 8 * i) : data >> (8 * i));
    }
    bl_spi_transfer(b, 4);
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        r |= (uint32_t)b[i] << (msb ? 24 - 8 * i : 8 * i);
    }
    return r;
}

void SPIClass::transferBytes(const uint8_t *out, uint8_t *in, uint32_t size)
{
    if (in) {
        if (out && out != in) {
            memcpy(in, out, size);
        } else if (!out) {
            memset(in, 0xFF, size);
        }
        bl_spi_transfer(in, size);
    } else if (out) {
        bl_spi_write(out, size);
    }
}
