/*
 * bl_apollo3_hw.cpp - Serial1, SPI, I2C buses, LEDC-style PWM and timers on
 * the Apollo3 (SparkFun's Apollo3 core, Mbed OS).
 *
 * The Apollo3's serial controllers (IOM0-5: I2C or SPI each, UART0-1) reach
 * fixed sets of pads, and Mbed stops the chip (error()) on a pad the
 * peripheral can't use. So Baseline checks every pin against Mbed's pin maps
 * first and creates the core's driver objects on the pins the sketch asks for.
 * One IOM is either an I2C or an SPI bus, not both.
 */
#include <mbed.h>
#include <SPI.h>
#include <Wire.h>
#include "PeripheralPins.h"
#include "bl_apollo3.h"

static uint32_t periph_of(int pin, const PinMap *map)
{
    if (pin < 0 || pin >= 50) {
        return (uint32_t)NC;
    }
    return pinmap_find_peripheral((PinName)pin, map);
}

/* ---------------- UART: port 1 = Serial1 (UART1); UART0 is Baseline's console ---------------- */

static UART *s_uart1;
static bool s_uart1_own;        /* s_uart1 was made here (not the core's Serial1) */

/* The SDK's SERIAL_* values are Arduino-ESP32's (parity in bits 0-1, data bits
 * in 2-3, stop bits in 4-5); the core takes ArduinoCore-API's. */
static uint16_t core_format(uint32_t config)
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
    if (port != 1) {
        return BL_ERR_UNSUPPORTED;      // two UARTs: UART0 is the console
    }
    if (rx < 0) {
        rx = BL_SERIAL1_RX;
    }
    if (tx < 0) {
        tx = BL_SERIAL1_TX;
    }
    if (!baud || periph_of(tx, PinMap_UART_TX) != UART_1 || periph_of(rx, PinMap_UART_RX) != UART_1) {
        return BL_ERR_ARG;
    }
    if (s_uart1_own) {
        delete s_uart1;
    }
    if (rx == BL_SERIAL1_RX && tx == BL_SERIAL1_TX) {
        s_uart1 = &Serial1;             // the variant's, on the board's Serial1 pins
        s_uart1_own = false;
    } else {
        s_uart1 = new UART((pin_size_t)tx, (pin_size_t)rx);
        s_uart1_own = true;
    }
    s_uart1->begin(baud, core_format(config));
    return BL_OK;
}

extern "C" void bl_impl_uart_end(int port)
{
    if (port == 1 && s_uart1) {
        s_uart1->end();
    }
}

extern "C" int bl_impl_uart_write(int port, const uint8_t *data, size_t len)
{
    return (port == 1 && s_uart1 && data) ? (int)s_uart1->write(data, len) : 0;
}

extern "C" int bl_impl_uart_available(int port)
{
    return (port == 1 && s_uart1) ? s_uart1->available() : 0;
}

extern "C" int bl_impl_uart_read(int port, uint8_t *buf, size_t size)
{
    if (port != 1 || !s_uart1 || !buf) {
        return 0;
    }
    size_t n = 0;
    while (n < size && s_uart1->available() > 0) {
        buf[n++] = (uint8_t)s_uart1->read();
    }
    return (int)n;
}

extern "C" int bl_impl_uart_peek(int port)
{
    return (port == 1 && s_uart1) ? s_uart1->peek() : -1;
}

extern "C" void bl_impl_uart_flush(int port)
{
    // The core's writes block until each byte is in the UART: nothing waits in a buffer.
}

extern "C" int bl_impl_uart_set_rx_buffer(int port, size_t size)
{
    // The core's receive buffer is fixed at 256 bytes.
    return port == 1 && size <= 256 ? BL_OK : BL_ERR_UNSUPPORTED;
}

extern "C" int bl_impl_uart_available_for_write(int port)
{
    return (port == 1 && s_uart1 && s_uart1->writeable()) ? 1 : 0;
}

/* ---------------- I2C and SPI: the IOM the pins belong to ---------------- */

static uint32_t s_i2c_iom[2] = {(uint32_t)NC, (uint32_t)NC};
static arduino::MbedI2C *s_wire[2];
static uint32_t s_spi_iom = (uint32_t)NC;
static arduino::MbedSPI *s_spi;

static bool iom_taken(uint32_t iom, int not_i2c_bus, bool not_spi)
{
    for (int b = 0; b < 2; b++) {
        if (b != not_i2c_bus && s_i2c_iom[b] == iom) {
            return true;
        }
    }
    return !not_spi && s_spi_iom == iom;
}

arduino::MbedI2C *bl_wire_object(int bus)
{
    return (bus == 0 || bus == 1) ? s_wire[bus] : NULL;
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
    uint32_t iom = periph_of(sda, PinMap_I2C_SDA);
    if (iom == (uint32_t)NC || periph_of(scl, PinMap_I2C_SCL) != iom) {
        return BL_ERR_ARG;
    }
    if (iom_taken(iom, bus, false)) {
        return BL_ERR_STATE;            // the other bus (or SPI) has that controller
    }
    if (s_wire[bus]) {
        s_wire[bus]->end();
        delete s_wire[bus];
    }
    s_wire[bus] = new arduino::MbedI2C(sda, scl);
    s_wire[bus]->begin();
    s_wire[bus]->setClock(freq ? freq : 100000);
    s_i2c_iom[bus] = iom;
    return BL_OK;
}

extern "C" void bl_impl_i2cx_end(int bus)
{
    arduino::MbedI2C *w = bl_wire_object(bus);
    if (w) {
        w->end();
        delete w;
        s_wire[bus] = NULL;
        s_i2c_iom[bus] = (uint32_t)NC;
    }
}

extern "C" int bl_impl_i2cx_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    arduino::MbedI2C *w = bl_wire_object(bus);
    if (!w) {
        return BL_ERR_STATE;
    }
    w->beginTransmission(addr);
    if (len) {
        w->write(data, (int)len);
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
    arduino::MbedI2C *w = bl_wire_object(bus);
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

extern "C" int bl_impl_spi_begin(int sck, int miso, int mosi, int ss)
{
    if (sck < 0) sck = BL_BOARD_SCK;
    if (miso < 0) miso = BL_BOARD_MISO;
    if (mosi < 0) mosi = BL_BOARD_MOSI;
    uint32_t iom = periph_of(sck, PinMap_SPI_SCLK);
    if (iom == (uint32_t)NC || periph_of(miso, PinMap_SPI_MISO) != iom || periph_of(mosi, PinMap_SPI_MOSI) != iom) {
        return BL_ERR_ARG;
    }
    if (iom_taken(iom, -1, true)) {
        return BL_ERR_STATE;            // an I2C bus has that controller
    }
    if (s_spi) {
        s_spi->end();
        delete s_spi;
    }
    s_spi = new arduino::MbedSPI((pin_size_t)miso, (pin_size_t)mosi, (pin_size_t)sck);
    s_spi->begin();             // the sketch drives chip select itself, as on the ESP32
    s_spi_iom = iom;
    return BL_OK;
}

extern "C" void bl_impl_spi_end(void)
{
    if (s_spi) {
        s_spi->end();
        delete s_spi;
        s_spi = NULL;
        s_spi_iom = (uint32_t)NC;
    }
}

extern "C" void bl_impl_spi_begin_transaction(uint32_t clock, int bit_order, int data_mode)
{
    static const SPIMode modes[4] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    if (s_spi) {
        s_spi->beginTransaction(SPISettings(clock, bit_order == BL_LSBFIRST ? LSBFIRST : MSBFIRST, modes[data_mode & 3]));
    }
}

extern "C" void bl_impl_spi_end_transaction(void)
{
    if (s_spi) {
        s_spi->endTransaction();
    }
}

extern "C" void bl_impl_spi_transfer(uint8_t *data, size_t len)
{
    if (s_spi && data && len) {
        s_spi->transfer(data, len);
    }
}

extern "C" void bl_impl_spi_write(const uint8_t *data, size_t len)
{
    uint8_t buf[64];            // the driver writes what it reads back into the buffer
    while (s_spi && data && len) {
        size_t n = len < sizeof(buf) ? len : sizeof(buf);
        memcpy(buf, data, n);
        s_spi->transfer(buf, n);
        data += n;
        len -= n;
    }
}

/* ---------------- LEDC-style PWM (the SDK's Servo, tone() and ledc*()) ----------------
 * On the core's per-pad CTIMER output (ap3_pwm_output): each pad has its own
 * period, counted in up to 16 bits. The clock is the fastest that fits the
 * frequency: 12 MHz down to 184 Hz, 3 MHz to 46 Hz (servos), 187.5 kHz to 3 Hz.
 * The two pads of one CTIMER segment share its timer, as two LEDC channels on
 * one timer do. */
extern ap3_err_t ap3_pwm_output(ap3_gpio_pad_t pad, uint32_t th, uint32_t fw, uint32_t clk);

struct LedcPin {
    uint32_t freq;
    uint32_t clk;       /* AM_HAL_CTIMER_* clock */
    uint32_t width;     /* period in clock ticks */
    uint8_t bits;       /* 0: not attached */
};
static LedcPin s_ledc[50];

static bool ledc_setup(int pin, uint32_t freq)
{
    static const struct { uint32_t clk, hz; } clocks[] = {
        {AM_HAL_CTIMER_HFRC_12MHZ, 12000000}, {AM_HAL_CTIMER_HFRC_3MHZ, 3000000}, {AM_HAL_CTIMER_HFRC_187_5KHZ, 187500},
    };
    if (!freq) {
        return false;
    }
    for (auto &c : clocks) {
        uint32_t width = c.hz / freq;
        if (width >= 3 && width <= 0xFFFF) {
            s_ledc[pin].freq = freq;
            s_ledc[pin].clk = c.clk;
            s_ledc[pin].width = width;
            return true;
        }
        if (width < 3) {
            return false;           // above 4 MHz
        }
    }
    return false;                   // below 3 Hz
}

static bool ledc_level(int pin, uint32_t duty)
{
    const LedcPin &p = s_ledc[pin];
    uint32_t full = 1u << p.bits;
    uint32_t th = duty >= full ? p.width : (uint32_t)(((uint64_t)duty * p.width) >> p.bits);
    return ap3_pwm_output((ap3_gpio_pad_t)pin, th, p.width, p.clk) == AP3_OK;
}

/* Whether a pad has a CTIMER output: ap3_pwm_output() says so when it is asked. */
static bool pwm_pin(int pin)
{
    return pin >= 0 && pin < 50 && pin != 48 && pin != 49;
}

extern "C" int bl_impl_ledc_attach(int pin, uint32_t freq, int resolution)
{
    if (!pwm_pin(pin) || resolution < 1 || resolution > 16 || !ledc_setup(pin, freq)) {
        return 0;
    }
    s_ledc[pin].bits = (uint8_t)resolution;
    if (!ledc_level(pin, 0)) {
        s_ledc[pin] = {};
        return 0;
    }
    return 1;
}

extern "C" int bl_impl_ledc_write(int pin, uint32_t duty)
{
    if (!pwm_pin(pin) || !s_ledc[pin].bits) {
        return 0;
    }
    return ledc_level(pin, duty) ? 1 : 0;
}

extern "C" int bl_impl_ledc_detach(int pin)
{
    if (!pwm_pin(pin) || !s_ledc[pin].bits) {
        return 0;
    }
    ap3_pwm_output((ap3_gpio_pad_t)pin, 0, 0, s_ledc[pin].clk);
    pinMode(pin, INPUT);
    s_ledc[pin] = {};
    return 1;
}

extern "C" uint32_t bl_impl_ledc_write_tone(int pin, uint32_t freq)
{
    if (!pwm_pin(pin)) {
        return 0;
    }
    if (!freq) {
        if (s_ledc[pin].bits) {
            ap3_pwm_output((ap3_gpio_pad_t)pin, 0, s_ledc[pin].width, s_ledc[pin].clk);
        }
        return 0;
    }
    if (!ledc_setup(pin, freq)) {
        return 0;
    }
    if (!s_ledc[pin].bits) {
        s_ledc[pin].bits = 8;
    }
    LedcPin &p = s_ledc[pin];
    return ap3_pwm_output((ap3_gpio_pad_t)pin, p.width / 2, p.width, p.clk) == AP3_OK ? freq : 0;   // a square wave
}

extern "C" uint32_t bl_impl_ledc_change_frequency(int pin, uint32_t freq, int resolution)
{
    if (!pwm_pin(pin) || !s_ledc[pin].bits) {
        return 0;
    }
    if (resolution >= 1 && resolution <= 16) {
        s_ledc[pin].bits = (uint8_t)resolution;
    }
    return ledc_setup(pin, freq) ? freq : 0;
}

/* ---------------- timers (Ticker, hw_timer, esp_timer): fired from bl_poll() ----------------
 * The sketch's callbacks run on its own thread, like on the ESP32 family. */

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

/* What the sketch started stops: timers, PWM, the buses, Serial1 (a sketch
 * restart after deep sleep, which doesn't reset the chip). */
void bl_hw_reset(void)
{
    memset(s_timers, 0, sizeof(s_timers));
    s_fired_head = s_fired_tail = 0;
    for (int pin = 0; pin < 50; pin++) {
        if (s_ledc[pin].bits) {
            bl_impl_ledc_detach(pin);
        }
    }
    bl_impl_i2cx_end(0);
    bl_impl_i2cx_end(1);
    bl_impl_spi_end();
    if (s_uart1) {
        s_uart1->end();
        if (s_uart1_own) {
            delete s_uart1;
        }
        s_uart1 = NULL;
        s_uart1_own = false;
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
