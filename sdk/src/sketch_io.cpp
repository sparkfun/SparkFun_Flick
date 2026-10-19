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
    bl_gpio_detach_interrupt(pin);
    if (pin < MAX_PINS) {
        s_handlers[pin] = PinHandler{};
    }
}

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
