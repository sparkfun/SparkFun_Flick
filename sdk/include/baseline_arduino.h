/*
 * baseline_arduino.h - Arduino-compatible C++ API for sketches (.ino/.cpp).
 * Included by baseline.h when compiling C++. These are thin layers over the
 * Baseline API table; the real work happens in Baseline on the board.
 *
 *   Serial / BLESerial        print, println, printf, read, available
 *   String, IPAddress         as in Arduino
 *   WiFi                      begin, status, localIP, macAddress, RSSI, mode
 *   HTTPClient                begin, addHeader, GET, POST, PUT, getString
 *   WiFiClientSecure          accepted by HTTPClient::begin (TLS uses Baseline's CA bundle)
 *   Wire                      I2C master on the Qwiic connector
 *   Serial1 / Serial2, SPI    see baseline_io.h
 *   Preferences, LittleFS     see baseline_storage.h
 *   MDNS, ArduinoOTA          see baseline_net.h
 *   PubSubClient              see baseline_mqtt.h
 *   ESP                       getFreeHeap, getChipModel, restart, ...
 *   esp_now_*                 see baseline_espnow.h
 *   random, map, constrain, min, max, log_i, ESP_LOGI
 */
#ifndef BASELINE_ARDUINO_H
#define BASELINE_ARDUINO_H

#ifdef __cplusplus

#include <string.h>

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define F(s) (s)

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

static const int STAT_LED = 0;      // blue STAT LED (SparkFun ESP32-S3 Thing Plus)
static const int LED_BUILTIN = 0;   // simple LED; Arduino's variant uses 46 (the RGB LED)
static const int RGB_BUILTIN = 46;  // WS2812 RGB LED
static const int SDA = 8;           // Qwiic
static const int SCL = 9;
static const int A0 = 10;

template <typename T, typename L, typename H> static inline T constrain(T x, L lo, H hi)
{
    return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x);
}
template <typename A, typename B> static inline A min(A a, B b) { return a < (A)b ? a : (A)b; }
template <typename A, typename B> static inline A max(A a, B b) { return a > (A)b ? a : (A)b; }
static inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* random() uses the hardware RNG, so randomSeed() has nothing to do. */
static inline void randomSeed(unsigned long) {}
static inline long random(long howbig) { return howbig > 0 ? (long)(bl_random() % (uint32_t)howbig) : 0; }
static inline long random(long howsmall, long howbig) { return howsmall >= howbig ? howsmall : howsmall + random(howbig - howsmall); }

#include "baseline_string.h"
#include "baseline_espnow.h"

class Print;

/* ---- Printable: objects that know how to print themselves ---- */
class Printable {
public:
    virtual size_t printTo(Print &p) const = 0;
};

/* ---- Print: Arduino's print/println/printf on top of write() ---- */
class Print {
public:
    virtual size_t write(const uint8_t *buf, size_t size) = 0;
    size_t write(uint8_t c) { return write(&c, 1); }
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t write(const char *buf, size_t size) { return write((const uint8_t *)buf, size); }

    size_t print(const char *s) { return write(s); }
    size_t print(const String &s) { return write(s.c_str(), s.length()); }
    size_t print(const Printable &x) { return x.printTo(*this); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(unsigned char n, int base = DEC) { return printNumber(n, base); }
    size_t print(int n, int base = DEC) { return base == DEC ? printSigned(n) : printNumber((unsigned int)n, base); }
    size_t print(unsigned int n, int base = DEC) { return printNumber(n, base); }
    size_t print(long n, int base = DEC) { return base == DEC ? printSigned(n) : printNumber((unsigned long)n, base); }
    size_t print(unsigned long n, int base = DEC) { return printNumber(n, base); }
    size_t print(long long n, int base = DEC) { return base == DEC ? printSigned(n) : printNumber((unsigned long long)n, base); }
    size_t print(unsigned long long n, int base = DEC) { return printNumber(n, base); }
    size_t print(double n, int digits = 2);

    size_t println() { return write("\r\n"); }
    template <typename T> size_t println(const T &value) { size_t n = print(value); return n + println(); }
    template <typename T> size_t println(const T &value, int format) { size_t n = print(value, format); return n + println(); }

    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

protected:
    size_t printNumber(unsigned long long n, int base);
    size_t printSigned(long long n);
};

/* ---- Stream: Print + input ---- */
class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    void setTimeout(unsigned long ms) { timeout_ = ms; }
    /* Read until `terminator` or `length` bytes; returns bytes stored (no NUL). */
    size_t readBytesUntil(char terminator, char *buffer, size_t length);
    String readStringUntil(char terminator);
    String readString();

protected:
    int timedRead();
    unsigned long timeout_ = 1000;
};

/* ---- Serial: the USB console ---- */
class SerialClass : public Stream {
public:
    void begin(unsigned long baud = 115200) {}  // USB CDC: baud does not matter
    void end() {}
    void flush() {}
    operator bool() const { return true; }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return (size_t)bl_console_write(buf, size); }
    int available() override { return bl_console_available(); }
    int read() override { return bl_console_read(); }
};

/* ---- BLESerial: BLE serial port (Nordic UART Service) ---- */
class BLESerialClass : public Stream {
public:
    bool begin(const char *name = "Baseline") { return bl_ble_serial_begin(name) == BL_OK; }
    bool connected() { return bl_ble_serial_connected() != 0; }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return (size_t)bl_ble_serial_write(buf, size); }
    int available() override { return bl_ble_serial_available(); }
    int read() override { return bl_ble_serial_read(); }
};

/* ---- IPAddress ---- */
class IPAddress : public Printable {
public:
    IPAddress() {}
    IPAddress(uint32_t v) : bytes_{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)} {}
    operator uint32_t() const { return bytes_[0] | (bytes_[1] << 8) | (bytes_[2] << 16) | ((uint32_t)bytes_[3] << 24); }
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}
    bool fromString(const char *text);
    bool fromString(const String &text) { return fromString(text.c_str()); }
    String toString() const;
    uint8_t operator[](int i) const { return bytes_[i & 3]; }
    uint8_t &operator[](int i) { return bytes_[i & 3]; }
    bool operator==(const IPAddress &o) const { return memcmp(bytes_, o.bytes_, 4) == 0; }
    bool operator!=(const IPAddress &o) const { return !(*this == o); }
    size_t printTo(Print &p) const override;

private:
    uint8_t bytes_[4] = {};
};

/* WiFi, WiFiClient/Server/UDP, HTTPClient, WebServer */
#include "baseline_net.h"

/* ---- Wire: I2C master on the Qwiic connector ---- */
class TwoWire : public Stream {
public:
    bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0) { return bl_i2c_begin(sda, scl, frequency) == BL_OK; }
    void beginTransmission(int address);
    uint8_t endTransmission(bool sendStop = true);  // 0 ok, 2 address NACK, 4 error, 5 timeout
    uint8_t requestFrom(int address, int quantity);
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override;
    int available() override { return (int)(rxLen_ - rxPos_); }
    int read() override { return rxPos_ < rxLen_ ? rxBuf_[rxPos_++] : -1; }

private:
    static const size_t BUFFER = 128;
    uint8_t txAddr_ = 0;
    uint8_t txBuf_[BUFFER] = {};
    size_t txLen_ = 0;
    uint8_t rxBuf_[BUFFER] = {};
    size_t rxLen_ = 0;
    size_t rxPos_ = 0;
};

/* ---- ESP: chip and memory information ---- */
class EspClass {
public:
    uint32_t getFreeHeap() { return bl_free_heap(); }
    uint32_t getHeapSize() { return bl_system_info(BL_INFO_HEAP_SIZE); }
    uint32_t getMinFreeHeap() { return bl_system_info(BL_INFO_MIN_FREE_HEAP); }
    uint32_t getMaxAllocHeap() { return bl_system_info(BL_INFO_MAX_ALLOC_HEAP); }
    uint32_t getPsramSize() { return bl_system_info(BL_INFO_PSRAM_SIZE); }
    uint32_t getFreePsram() { return bl_system_info(BL_INFO_FREE_PSRAM); }
    uint32_t getFlashChipSize() { return bl_system_info(BL_INFO_FLASH_SIZE); }
    uint32_t getCpuFreqMHz() { return bl_system_info(BL_INFO_CPU_MHZ); }
    const char *getChipModel() { return "ESP32-S3"; }
    uint8_t getChipCores() { return 2; }
    const char *getSdkVersion() { return bl_baseline_version(); }     // the Baseline version
    uint64_t getEfuseMac();
    void restart() { bl_restart(); }
};

extern SerialClass Serial;
extern BLESerialClass BLESerial;
extern TwoWire Wire;
extern EspClass ESP;

/* attachInterrupt, SPI, Serial1/Serial2 */
#include "baseline_io.h"
/* Arduino BLE library: BLEDevice, BLEServer, BLEScan, BLEClient, ... */
#include "baseline_ble.h"
/* Preferences, LittleFS / File */
#include "baseline_storage.h"
/* PubSubClient (MQTT) */
#include "baseline_mqtt.h"

#endif /* __cplusplus */
#endif /* BASELINE_ARDUINO_H */
