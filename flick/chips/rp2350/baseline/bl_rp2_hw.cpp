/*
 * bl_rp2_hw.cpp - UARTs, SPI, I2C buses, LEDC-style PWM and timers on the
 * RP2350 (arduino-pico).
 *
 * The RP2350's peripherals sit on fixed groups of pins (GPIO function select),
 * and arduino-pico stops the chip (panic) on a pin the peripheral can't use.
 * So Baseline checks every pin first, and picks the I2C and SPI controller the
 * pins belong to: Wire is whichever controller the sketch's pins need.
 */
#include <SPI.h>
#include <Wire.h>
#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include "bl_rp2.h"

/* ---------------- UARTs: port 1 = Serial1 (UART0), 2 = Serial2 (UART1) ----------------
 * Bank-0 pins come in groups of four: TX, RX, CTS, RTS; the groups alternate
 * UART0, UART1, UART1, UART0, ... (0-3 UART0, 4-11 UART1, 12-19 UART0, ...). */

static int uart_of_pin(int pin)
{
    return ((pin + 4) >> 3) & 1;
}

static SerialUART *uart(int port)
{
    return port == 1 ? &Serial1 : port == 2 ? &Serial2 : NULL;
}

static bool uart_pin_ok(int port, int pin, int role)     /* role 0: TX, 1: RX */
{
    return pin >= 0 && pin < __GPIOCNT && uart_of_pin(pin) == port - 1 && (pin & 3) == role;
}

static size_t s_rx_size[3];

/* The SDK's SERIAL_* values are Arduino-ESP32's (parity in bits 0-1, data bits
 * in 2-3, stop bits in 4-5); arduino-pico takes ArduinoCore-API's. */
static int pico_format(uint32_t config)
{
    int parity = config & 3, bits = (config >> 2) & 3, stop = (config >> 4) & 3;
    uint16_t f = parity == 2 ? SERIAL_PARITY_EVEN : parity == 3 ? SERIAL_PARITY_ODD : SERIAL_PARITY_NONE;
    static const uint16_t data[4] = {SERIAL_DATA_5, SERIAL_DATA_6, SERIAL_DATA_7, SERIAL_DATA_8};
    f |= data[bits];
    f |= stop == 3 ? SERIAL_STOP_BIT_2 : SERIAL_STOP_BIT_1;
    return f;
}

extern "C" int bl_impl_uart_begin(int port, uint32_t baud, uint32_t config, int rx, int tx)
{
    SerialUART *u = uart(port);
    if (!u) {
        return BL_ERR_UNSUPPORTED;
    }
    if (rx < 0) {
        rx = port == 1 ? BL_SERIAL1_RX : BL_SERIAL2_RX;
    }
    if (tx < 0) {
        tx = port == 1 ? BL_SERIAL1_TX : BL_SERIAL2_TX;
    }
    if (!baud || !uart_pin_ok(port, rx, 1) || !uart_pin_ok(port, tx, 0)) {
        return BL_ERR_ARG;
    }
    u->end();
    u->setRX(rx);
    u->setTX(tx);
    if (s_rx_size[port]) {
        u->setFIFOSize(s_rx_size[port]);
    }
    u->begin(baud, (uint16_t)pico_format(config));
    return BL_OK;
}

extern "C" void bl_impl_uart_end(int port)
{
    SerialUART *u = uart(port);
    if (u) {
        u->end();
    }
}

extern "C" int bl_impl_uart_write(int port, const uint8_t *data, size_t len)
{
    SerialUART *u = uart(port);
    return (u && data) ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_uart_available(int port)
{
    SerialUART *u = uart(port);
    return u ? u->available() : 0;
}

extern "C" int bl_impl_uart_read(int port, uint8_t *buf, size_t size)
{
    SerialUART *u = uart(port);
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
    SerialUART *u = uart(port);
    return u ? u->peek() : -1;
}

extern "C" void bl_impl_uart_flush(int port)
{
    SerialUART *u = uart(port);
    if (u) {
        u->flush();
    }
}

extern "C" int bl_impl_uart_set_rx_buffer(int port, size_t size)
{
    if (!uart(port)) {
        return BL_ERR_UNSUPPORTED;
    }
    s_rx_size[port] = size;     // applied at the next begin, as on the ESP32
    return BL_OK;
}

extern "C" int bl_impl_uart_available_for_write(int port)
{
    SerialUART *u = uart(port);
    return u ? u->availableForWrite() : 0;
}

/* ---------------- SPI: the controller the pins belong to ----------------
 * Groups of eight pins alternate SPI0, SPI1; in each group of four the pins are
 * RX (MISO), CSn, SCK, TX (MOSI). */

static SPIClassRP2040 *s_spi = &SPI;

static int spi_of_pin(int pin)
{
    return (pin >> 3) & 1;
}

static bool spi_pin_ok(int ctl, int pin, int role)       /* role: 0 MISO, 2 SCK, 3 MOSI */
{
    return pin >= 0 && pin < __GPIOCNT && spi_of_pin(pin) == ctl && (pin & 3) == role;
}

extern "C" int bl_impl_spi_begin(int sck, int miso, int mosi, int ss)
{
    if (sck < 0) sck = BL_BOARD_SCK;
    if (miso < 0) miso = BL_BOARD_MISO;
    if (mosi < 0) mosi = BL_BOARD_MOSI;
    int ctl = spi_of_pin(sck);
    if (!spi_pin_ok(ctl, sck, 2) || !spi_pin_ok(ctl, miso, 0) || !spi_pin_ok(ctl, mosi, 3)) {
        return BL_ERR_ARG;
    }
    s_spi->end();
    s_spi = ctl == 0 ? &SPI : &SPI1;
    s_spi->setSCK(sck);
    s_spi->setRX(miso);
    s_spi->setTX(mosi);
    s_spi->begin(false);        // the sketch drives chip select itself, as on the ESP32
    return BL_OK;
}

extern "C" void bl_impl_spi_end(void)
{
    s_spi->end();
}

extern "C" void bl_impl_spi_begin_transaction(uint32_t clock, int bit_order, int data_mode)
{
    static const SPIMode modes[4] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    s_spi->beginTransaction(SPISettings(clock, bit_order == BL_LSBFIRST ? LSBFIRST : MSBFIRST, modes[data_mode & 3]));
}

extern "C" void bl_impl_spi_end_transaction(void)
{
    s_spi->endTransaction();
}

extern "C" void bl_impl_spi_transfer(uint8_t *data, size_t len)
{
    if (data && len) {
        s_spi->transfer(data, len);
    }
}

extern "C" void bl_impl_spi_write(const uint8_t *data, size_t len)
{
    if (data && len) {
        s_spi->transfer(data, NULL, len);
    }
}

/* SPIClassRP2040 for SD.begin(): the controller spi_begin picked. */
SPIClassRP2040 *bl_spi_object(void)
{
    return s_spi;
}

/* ---------------- I2C: API bus 0 (Wire) and 1 (Wire1), each on the controller its pins need ----------------
 * Pins pair up SDA (even), SCL (odd); the pairs alternate I2C0, I2C1. */

#ifndef __WIRE0_DEVICE
#define __WIRE0_DEVICE i2c0     /* arduino-pico's default (Wire.cpp); a variant may swap them */
#endif

static TwoWire *controller(int ctl)
{
    return (ctl == 0) == (__WIRE0_DEVICE == i2c0) ? &Wire : &Wire1;
}

static int s_bus_ctl[2] = {-1, -1};

TwoWire *bl_wire_object(int bus)
{
    return (bus == 0 || bus == 1) && s_bus_ctl[bus] >= 0 ? controller(s_bus_ctl[bus]) : NULL;
}

extern "C" int bl_impl_i2cx_begin(int bus, int sda, int scl, uint32_t freq)
{
    if (bus != 0 && bus != 1) {
        return BL_ERR_UNSUPPORTED;
    }
    if (sda < 0 || scl < 0) {
        if (bus == 1) {
            return BL_ERR_ARG;          // Wire1 has no default pins
        }
        sda = BL_BOARD_SDA;
        scl = BL_BOARD_SCL;
    }
    int ctl = (sda >> 1) & 1;
    if (sda >= __GPIOCNT || scl >= __GPIOCNT || (sda & 1) || scl != (scl | 1) || ((scl >> 1) & 1) != ctl) {
        return BL_ERR_ARG;
    }
    if (s_bus_ctl[bus ^ 1] == ctl) {
        return BL_ERR_STATE;            // the other bus has that controller
    }
    if (s_bus_ctl[bus] >= 0) {
        controller(s_bus_ctl[bus])->end();
    }
    TwoWire *w = controller(ctl);
    w->setSDA(sda);
    w->setSCL(scl);
    w->setClock(freq ? freq : 100000);
    w->begin();
    s_bus_ctl[bus] = ctl;
    return BL_OK;
}

extern "C" void bl_impl_i2cx_end(int bus)
{
    TwoWire *w = bl_wire_object(bus);
    if (w) {
        w->end();
        s_bus_ctl[bus] = -1;
    }
}

extern "C" int bl_impl_i2cx_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    TwoWire *w = bl_wire_object(bus);
    if (!w) {
        return BL_ERR_STATE;
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
    TwoWire *w = bl_wire_object(bus);
    if (!w) {
        return BL_ERR_STATE;
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
    return bl_impl_i2cx_begin(0, sda, scl, freq);
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
 * Straight on the PWM slices: arduino-pico's analogWrite has one frequency for
 * every pin, and Servo needs 50 Hz next to an LED at 5 kHz. The two pins of a
 * slice share its frequency, as two LEDC channels on one timer do. Any
 * resolution: the slice counts to whatever top gives the frequency, and duty
 * is scaled to it. */

struct LedcPin {
    uint32_t freq;
    uint8_t bits;       /* 0: not attached */
    uint16_t top;
};
static LedcPin s_ledc[__GPIOCNT];

static bool ledc_setup(int pin, uint32_t freq)
{
    uint32_t sys = clock_get_hz(clk_sys);
    if (!freq || freq > sys / 2) {
        return false;
    }
    uint32_t div16 = (uint32_t)(((uint64_t)sys * 16 + (uint64_t)freq * 65536 - 1) / ((uint64_t)freq * 65536));
    if (div16 < 16) {
        div16 = 16;             // integer divider of at least 1
    }
    if (div16 > 255 * 16 + 15) {
        return false;           // below about 9 Hz at 150 MHz
    }
    uint32_t top = (uint32_t)(((uint64_t)sys * 16) / ((uint64_t)div16 * freq)) - 1;
    if (top > 65535) {
        top = 65535;
    }
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac4(&c, div16 >> 4, div16 & 15);
    pwm_config_set_wrap(&c, top);
    pwm_init(slice, &c, true);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    s_ledc[pin].freq = freq;
    s_ledc[pin].top = (uint16_t)top;
    return true;
}

static void ledc_level(int pin, uint32_t duty)
{
    const LedcPin &p = s_ledc[pin];
    uint32_t full = 1u << p.bits;
    uint32_t level = duty >= full ? p.top + 1u : (uint32_t)(((uint64_t)duty * (p.top + 1u)) >> p.bits);
    pwm_set_gpio_level(pin, (uint16_t)(level > 65535 ? 65535 : level));
}

extern "C" int bl_impl_ledc_attach(int pin, uint32_t freq, int resolution)
{
    if (pin < 0 || pin >= __GPIOCNT || resolution < 1 || resolution > 16 || !ledc_setup(pin, freq)) {
        return 0;
    }
    s_ledc[pin].bits = (uint8_t)resolution;
    ledc_level(pin, 0);
    return 1;
}

extern "C" int bl_impl_ledc_write(int pin, uint32_t duty)
{
    if (pin < 0 || pin >= __GPIOCNT || !s_ledc[pin].bits) {
        return 0;
    }
    ledc_level(pin, duty);
    return 1;
}

extern "C" int bl_impl_ledc_detach(int pin)
{
    if (pin < 0 || pin >= __GPIOCNT || !s_ledc[pin].bits) {
        return 0;
    }
    pwm_set_gpio_level(pin, 0);
    pinMode(pin, INPUT);
    s_ledc[pin] = {};
    return 1;
}

extern "C" uint32_t bl_impl_ledc_write_tone(int pin, uint32_t freq)
{
    if (pin < 0 || pin >= __GPIOCNT) {
        return 0;
    }
    if (!freq) {
        if (s_ledc[pin].bits) {
            pwm_set_gpio_level(pin, 0);
        }
        return 0;
    }
    if (!ledc_setup(pin, freq)) {
        return 0;
    }
    if (!s_ledc[pin].bits) {
        s_ledc[pin].bits = 8;
    }
    pwm_set_gpio_level(pin, (uint16_t)((s_ledc[pin].top + 1u) / 2));     // a square wave
    return freq;
}

extern "C" uint32_t bl_impl_ledc_change_frequency(int pin, uint32_t freq, int resolution)
{
    if (pin < 0 || pin >= __GPIOCNT || !s_ledc[pin].bits) {
        return 0;
    }
    if (resolution >= 1 && resolution <= 16) {
        s_ledc[pin].bits = (uint8_t)resolution;
    }
    return ledc_setup(pin, freq) ? freq : 0;
}

/* ---------------- timers (Ticker, hw_timer, esp_timer): fired from bl_poll() ----------------
 * The sketch's callbacks run on its own context, like on the ESP32 family. */

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
    uint64_t now = time_us_64();
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
    t->next = time_us_64() + period_us;
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
