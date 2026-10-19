/*
 * baseline_io.h - attachInterrupt, SPI and Serial1/Serial2 for sketches.
 * Included by baseline_arduino.h.
 *
 * Interrupts are deferred: Baseline's ISR records the pin, and your handler
 * runs on the sketch task during delay()/yield() or between loop() calls.
 * Handlers may therefore use Serial, delay, and any sketch variable safely,
 * but latency is a few milliseconds rather than microseconds. IRAM_ATTR is
 * accepted and ignored. noInterrupts()/interrupts() are not needed (handlers
 * never preempt sketch code) and do nothing.
 */
#ifndef BASELINE_IO_H
#define BASELINE_IO_H

#ifdef __cplusplus

/* ---------------- interrupts ---------------- */

#define RISING  BL_RISING
#define FALLING BL_FALLING
#define CHANGE  BL_CHANGE
#define ONLOW   BL_ONLOW
#define ONHIGH  BL_ONHIGH

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define ARDUINO_ISR_ATTR
#define digitalPinToInterrupt(p) (p)

void attachInterrupt(uint8_t pin, void (*handler)(void), int mode);
void attachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, int mode);
void detachInterrupt(uint8_t pin);
static inline void noInterrupts() {}
static inline void interrupts() {}

/* ---------------- SPI ---------------- */

#define LSBFIRST  BL_LSBFIRST
#define MSBFIRST  BL_MSBFIRST
#define SPI_LSBFIRST BL_LSBFIRST
#define SPI_MSBFIRST BL_MSBFIRST
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings {
public:
    constexpr SPISettings(uint32_t clock = 1000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0)
        : clock_(clock), bitOrder_(bitOrder), dataMode_(dataMode) {}
    uint32_t clock_;
    uint8_t bitOrder_;
    uint8_t dataMode_;
};

class SPIClass {
public:
    /* -1 = the board's default SCK/MISO/MOSI/SS pins */
    bool begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1)
    {
        return bl_spi_begin(sck, miso, mosi, ss) == BL_OK;
    }
    void end() { bl_spi_end(); }
    void beginTransaction(SPISettings s)
    {
        settings_ = s;
        bl_spi_begin_transaction(s.clock_, s.bitOrder_, s.dataMode_);
    }
    void endTransaction() { bl_spi_end_transaction(); }
    void setFrequency(uint32_t freq) { settings_.clock_ = freq; apply(); }
    void setBitOrder(uint8_t order) { settings_.bitOrder_ = order; apply(); }
    void setDataMode(uint8_t mode) { settings_.dataMode_ = mode; apply(); }

    uint8_t transfer(uint8_t data) { bl_spi_transfer(&data, 1); return data; }
    uint16_t transfer16(uint16_t data);
    uint32_t transfer32(uint32_t data);
    void transfer(void *data, uint32_t size) { bl_spi_transfer((uint8_t *)data, size); }
    void transferBytes(const uint8_t *out, uint8_t *in, uint32_t size);
    void write(uint8_t data) { bl_spi_write(&data, 1); }
    void write16(uint16_t data) { transfer16(data); }
    void write32(uint32_t data) { transfer32(data); }
    void writeBytes(const uint8_t *data, uint32_t size) { bl_spi_write(data, size); }

private:
    void apply()
    {
        bl_spi_begin_transaction(settings_.clock_, settings_.bitOrder_, settings_.dataMode_);
        bl_spi_end_transaction();
    }
    SPISettings settings_;
};

extern SPIClass SPI;

/* ---------------- Hardware UARTs ----------------
 * Serial1: default pins RX 44 / TX 43 (the board's RX/TX labels).
 * Serial2: pass the pins, e.g. Serial2.begin(9600, SERIAL_8N1, 16, 17).
 * Serial (USB) is the console; see baseline_arduino.h. */

/* Frame formats: same values as Arduino-ESP32 */
#define SERIAL_5N1 0x8000010
#define SERIAL_6N1 0x8000014
#define SERIAL_7N1 0x8000018
#define SERIAL_8N1 0x800001c
#define SERIAL_5N2 0x8000030
#define SERIAL_6N2 0x8000034
#define SERIAL_7N2 0x8000038
#define SERIAL_8N2 0x800003c
#define SERIAL_5E1 0x8000012
#define SERIAL_6E1 0x8000016
#define SERIAL_7E1 0x800001a
#define SERIAL_8E1 0x800001e
#define SERIAL_5E2 0x8000032
#define SERIAL_6E2 0x8000036
#define SERIAL_7E2 0x800003a
#define SERIAL_8E2 0x800003e
#define SERIAL_5O1 0x8000013
#define SERIAL_6O1 0x8000017
#define SERIAL_7O1 0x800001b
#define SERIAL_8O1 0x800001f
#define SERIAL_5O2 0x8000033
#define SERIAL_6O2 0x8000037
#define SERIAL_7O2 0x800003b
#define SERIAL_8O2 0x800003f

class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(int port) : port_(port) {}
    /* Extra Arduino arguments (invert, timeout, rx threshold) are accepted and ignored. */
    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin = -1,
               bool invert = false, unsigned long timeout_ms = 20000UL, uint8_t rxfifo_full_thrhd = 120)
    {
        started_ = bl_uart_begin(port_, baud, config, rxPin, txPin) == BL_OK;
    }
    void end()
    {
        bl_uart_end(port_);
        started_ = false;
    }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return started_ ? (size_t)bl_uart_write(port_, buf, size) : 0; }
    int available() override { return started_ ? bl_uart_available(port_) : 0; }
    int read() override;
    size_t read(uint8_t *buf, size_t size) { return started_ ? (size_t)bl_uart_read(port_, buf, size) : 0; }
    size_t readBytes(uint8_t *buf, size_t size) { return read(buf, size); }
    int peek() { return started_ ? bl_uart_peek(port_) : -1; }
    void flush() { if (started_) bl_uart_flush(port_); }
    operator bool() const { return started_; }

private:
    int port_;
    bool started_ = false;
};

extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

#endif /* __cplusplus */
#endif /* BASELINE_IO_H */
