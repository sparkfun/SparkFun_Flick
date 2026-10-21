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

/* ---------------- touch pins ----------------
 * touchRead() values differ by chip: on the classic ESP32 they DROP when
 * touched (interrupt below the threshold); on the S3 they RISE (interrupt
 * above). The handler runs on the sketch task, like attachInterrupt. */
typedef uint32_t touch_value_t;
#ifdef BL_TOUCH_T0
static const uint8_t T0 = BL_TOUCH_T0;
#endif
#ifdef BL_TOUCH_T1
static const uint8_t T1 = BL_TOUCH_T1, T2 = BL_TOUCH_T2, T3 = BL_TOUCH_T3, T4 = BL_TOUCH_T4, T5 = BL_TOUCH_T5,
                     T6 = BL_TOUCH_T6, T7 = BL_TOUCH_T7, T8 = BL_TOUCH_T8, T9 = BL_TOUCH_T9;
#endif
#ifdef BL_TOUCH_T10
static const uint8_t T10 = BL_TOUCH_T10, T11 = BL_TOUCH_T11, T12 = BL_TOUCH_T12, T13 = BL_TOUCH_T13, T14 = BL_TOUCH_T14;
#endif

static inline touch_value_t touchRead(uint8_t pin) { return bl_touch_read(pin); }
void touchAttachInterrupt(uint8_t pin, void (*handler)(void), touch_value_t threshold);
void touchAttachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, touch_value_t threshold);
void touchDetachInterrupt(uint8_t pin);
/* Deep sleep wake by touch: set the pin's threshold, then enable touch wake. */
static inline void touchSleepWakeUpEnable(uint8_t pin, touch_value_t threshold)
{
    bl_touch_attach(pin, threshold);
    bl_sleep_wake(BL_WAKE_TOUCH, 0, 0, 0);
}

/* ---------------- LEDC PWM (any frequency / resolution) ---------------- */
static inline bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution) { return bl_ledc_attach(pin, freq, resolution) == 1; }
static inline bool ledcWrite(uint8_t pin, uint32_t duty) { return bl_ledc_write(pin, duty) == 1; }
static inline bool ledcDetach(uint8_t pin) { return bl_ledc_detach(pin) == 1; }
static inline uint32_t ledcWriteTone(uint8_t pin, uint32_t freq) { return bl_ledc_write_tone(pin, freq); }
static inline uint32_t ledcChangeFrequency(uint8_t pin, uint32_t freq, uint8_t resolution) { return bl_ledc_change_frequency(pin, freq, resolution); }

/* ---------------- Servo (ESP32Servo-compatible) ----------------
 * 50 Hz, 14-bit LEDC. write() takes degrees (0-180), or microseconds when the
 * value is 500 or more, as in the Arduino Servo library. */
#define MIN_PULSE_WIDTH     544
#define MAX_PULSE_WIDTH     2400
#define DEFAULT_PULSE_WIDTH 1500

class Servo {
public:
    int attach(int pin, int min_us = MIN_PULSE_WIDTH, int max_us = MAX_PULSE_WIDTH);
    void detach();
    void write(int value);
    void writeMicroseconds(int us);
    int read() { return map(us_, min_, max_, 0, 180); }
    int readMicroseconds() { return us_; }
    bool attached() { return pin_ >= 0; }
    void setPeriodHertz(int hz) { hz_ = hz > 0 ? hz : 50; if (pin_ >= 0) { bl_ledc_change_frequency(pin_, hz_, 14); writeMicroseconds(us_); } }

private:
    int pin_ = -1;
    int min_ = MIN_PULSE_WIDTH;
    int max_ = MAX_PULSE_WIDTH;
    int us_ = DEFAULT_PULSE_WIDTH;
    int hz_ = 50;
};

/* ---------------- I2S (Arduino-ESP32 3.x ESP_I2S) ----------------
 * One I2S port. Mono/stereo samples of 8/16/24/32 bits, standard I2S or PDM
 * (PDM microphones: setPinsPdmRx(clk, data); PDM speakers/amps: setPinsPdmTx). */
typedef enum { I2S_MODE_STD = BL_I2S_STD, I2S_MODE_TDM = BL_I2S_TDM, I2S_MODE_PDM_TX = BL_I2S_PDM_TX, I2S_MODE_PDM_RX = BL_I2S_PDM_RX } i2s_mode_t;
typedef enum {
    I2S_DATA_BIT_WIDTH_8BIT = 8, I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_24BIT = 24, I2S_DATA_BIT_WIDTH_32BIT = 32,
} i2s_data_bit_width_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;

class I2SClass : public Stream {
public:
    void setPins(int8_t bclk, int8_t ws, int8_t dout, int8_t din = -1, int8_t mclk = -1)
    {
        bclk_ = bclk; ws_ = ws; dout_ = dout; din_ = din; mclk_ = mclk;
    }
    void setPinsPdmTx(int8_t clk, int8_t dout0, int8_t dout1 = -1) { bclk_ = clk; dout_ = dout0; ws_ = -1; din_ = -1; }
    void setPinsPdmRx(int8_t clk, int8_t din0, int8_t din1 = -1, int8_t din2 = -1, int8_t din3 = -1) { bclk_ = clk; din_ = din0; ws_ = -1; dout_ = -1; }
    bool begin(i2s_mode_t mode, uint32_t rate, i2s_data_bit_width_t bits, i2s_slot_mode_t ch, int8_t slot_mask = -1)
    {
        return bl_i2s_begin(mode, rate, bits, ch == I2S_SLOT_MODE_STEREO, bclk_, ws_, dout_, din_, mclk_) == BL_OK;
    }
    bool end() { bl_i2s_end(); return true; }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return (size_t)bl_i2s_write(buf, size); }
    size_t write(const void *buf, size_t size) { return write((const uint8_t *)buf, size); }
    size_t readBytes(char *buffer, size_t size);
    int available() override;      // blocks briefly to fetch samples
    int read() override;
    int lastError() { return 0; }

private:
    int8_t bclk_ = -1, ws_ = -1, dout_ = -1, din_ = -1, mclk_ = -1;
    uint8_t rx_[128] = {};
    uint16_t rxLen_ = 0, rxPos_ = 0;
};

#endif /* __cplusplus */
#endif /* BASELINE_IO_H */
