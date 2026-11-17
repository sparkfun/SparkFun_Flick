/*
 * baseline_arduino.h - Arduino-compatible C++ API for sketches (.ino/.cpp).
 * Included by baseline.h when compiling C++. These are thin layers over the
 * Baseline API table; the real work happens in Baseline on the board.
 *
 *   Serial / BLESerial        print, println, printf, read, available
 *   BluetoothSerial           Bluetooth Classic SPP (classic ESP32 only)
 *   String, IPAddress         as in Arduino
 *   WiFi                      begin, status, localIP, macAddress, RSSI, mode
 *   HTTPClient                begin, addHeader, GET, POST, PUT, getString
 *   WiFiClientSecure          accepted by HTTPClient::begin (TLS uses Baseline's CA bundle)
 *   Wire                      I2C master on the Qwiic connector
 *   Serial1 / Serial2, SPI    see baseline_io.h
 *   Preferences, LittleFS     see baseline_storage.h
 *   MDNS, ArduinoOTA          see baseline_net.h
 *   PubSubClient              see baseline_mqtt.h
 *   SD, SD_MMC                see baseline_storage.h
 *   sleep, time, timers, tone see baseline_system.h
 *   touch, LEDC, Servo, I2S   see baseline_io.h
 *   FastLED                   see baseline_fastled.h
 *   DNSServer                 see baseline_net.h
 *   USB HID / MIDI            see baseline_usb.h (ESP32-S3)
 *   SFE_MAX1704X              see baseline_fuelgauge.h
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

/* F("text"): a string kept in flash, as on AVR. On the ESP32 family every
 * string constant is already in flash; the type exists so that Print, String
 * and libraries can overload on it. */
class __FlashStringHelper;
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper *>(p))
#define F(s) FPSTR(PSTR(s))

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

/* Arduino's bit, byte and math helpers. */
#ifndef PI
#define PI          3.1415926535897932384626433832795
#endif
#define HALF_PI     1.5707963267948966192313216916398
#define TWO_PI      6.283185307179586476925286766559
#define DEG_TO_RAD  0.017453292519943295769236907684886
#define RAD_TO_DEG  57.295779513082320876798154814105
#define EULER       2.718281828459045235360287471352
#define radians(deg)            ((deg) * DEG_TO_RAD)
#define degrees(rad)            ((rad) * RAD_TO_DEG)
#define sq(x)                   ((x) * (x))
#define lowByte(w)              ((uint8_t)((w) & 0xff))
#define highByte(w)             ((uint8_t)((w) >> 8))
#define bitRead(value, bit)     (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)      ((value) |= (1UL << (bit)))
#define bitClear(value, bit)    ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit)   ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, b) ((b) ? bitSet(value, bit) : bitClear(value, bit))
#ifndef bit
#define bit(b)                  (1UL << (b))
#endif
static inline uint16_t makeWord(uint8_t h, uint8_t l) { return (uint16_t)((h << 8) | l); }

/* Arduino's character tests (WCharacter.h), on <ctype.h> (ASCII). */
inline bool isAlphaNumeric(int c) { return isalnum(c) != 0; }
inline bool isAlpha(int c) { return isalpha(c) != 0; }
inline bool isAscii(int c) { return isascii(c) != 0; }
inline bool isWhitespace(int c) { return isblank(c) != 0; }
inline bool isControl(int c) { return iscntrl(c) != 0; }
inline bool isDigit(int c) { return isdigit(c) != 0; }
inline bool isGraph(int c) { return isgraph(c) != 0; }
inline bool isLowerCase(int c) { return islower(c) != 0; }
inline bool isPrintable(int c) { return isprint(c) != 0; }
inline bool isPunct(int c) { return ispunct(c) != 0; }
inline bool isSpace(int c) { return isspace(c) != 0; }
inline bool isUpperCase(int c) { return isupper(c) != 0; }
inline bool isHexadecimalDigit(int c) { return isxdigit(c) != 0; }
inline int toAscii(int c) { return toascii(c); }
inline int toLowerCase(int c) { return tolower(c); }
inline int toUpperCase(int c) { return toupper(c); }

/* Board pins: from the target's bl_target.h (targets/<target>/sdk/). */
#include "bl_target.h"
static const int STAT_LED = BL_BOARD_STAT_LED;
static const int LED_BUILTIN = BL_BOARD_LED_BUILTIN;
#ifdef BL_BOARD_RGB_LED
#define RGB_BUILTIN BL_BOARD_RGB_LED        // WS2812 RGB LED (a macro, so sketches can #ifdef it)
#endif
static const int SDA = BL_BOARD_SDA;        // I2C (Qwiic on SparkFun boards)
static const int SCL = BL_BOARD_SCL;
static const int A0 = BL_BOARD_A0;

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
#if defined(BL_HAS_ESPNOW)
#include "baseline_espnow.h"
#endif

class Print;

/* ---- Printable: objects that know how to print themselves ---- */
class Printable {
public:
    virtual size_t printTo(Print &p) const = 0;
};

/* ---- Print: Arduino's print/println/printf on top of write() ---- */
class Print {
public:
    /* Override either one, as in Arduino: libraries usually implement
     * write(uint8_t), the SDK's own classes the buffer version. */
    virtual size_t write(uint8_t c) { return write(&c, 1); }
    virtual size_t write(const uint8_t *buf, size_t size)
    {
        size_t n = 0;
        while (size-- && write(*buf++)) {
            n++;
        }
        return n;
    }
    virtual int availableForWrite() { return 0; }
    virtual void flush() {}
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t write(const char *buf, size_t size) { return write((const uint8_t *)buf, size); }

    size_t print(const char *s) { return write(s); }
    size_t print(const __FlashStringHelper *s) { return write(reinterpret_cast<const char *>(s)); }
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
    virtual int peek() { return -1; }
    void setTimeout(unsigned long ms) { timeout_ = ms; }
    unsigned long getTimeout() const { return timeout_; }
    /* Read up to `length` bytes, waiting up to the timeout for each. */
    size_t readBytes(char *buffer, size_t length);
    size_t readBytes(uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }
    bool find(const char *target);
    bool find(char target) { char t[2] = {target, 0}; return find(t); }
    bool findUntil(const char *target, const char *terminator);
    long parseInt();
    float parseFloat();
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
    int availableForWrite() { return 128; }     // console writes queue in Baseline and never fail
    size_t setRxBufferSize(size_t size) { return size; }   // the console's buffer is Baseline's (8 KB)
    size_t setTxBufferSize(size_t size) { return size; }
    void setDebugOutput(bool) {}
};

#if defined(BL_HAS_BLE)
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

#endif /* BL_HAS_BLE */
#if defined(BL_HAS_BT_CLASSIC)
/* ---- BluetoothSerial: Bluetooth Classic serial port (SPP) ----
 * Classic ESP32 only (the S3, C3, C6 have no Bluetooth Classic): elsewhere
 * begin() returns false. Phones and PCs pair with it as a serial port. */
class BluetoothSerial : public Stream {
public:
    bool begin(const String &localName = String(), bool isMaster = false, bool disableBLE = false)
    {
        return bl_bt_spp_begin(localName.length() ? localName.c_str() : "ESP32") == BL_OK;
    }
    void end() { bl_bt_spp_end(); }
    bool hasClient() { return bl_bt_spp_connected() == 1; }
    /* Wait up to timeout ms for a device to connect (0 = just check). */
    bool connected(int timeout = 0)
    {
        uint32_t start = millis();
        while (!hasClient() && (int)(millis() - start) < timeout) {
            delay(10);
        }
        return hasClient();
    }
    bool setPin(const char *pin, uint8_t pin_code_len)
    {
        char buf[17] = {};
        memcpy(buf, pin, pin_code_len < 16 ? pin_code_len : 16);
        return bl_bt_spp_set_pin(buf) == BL_OK;
    }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return (size_t)bl_bt_spp_write(buf, size); }
    int available() override { return bl_bt_spp_available(); }
    int read() override
    {
        uint8_t b;
        return bl_bt_spp_read(&b, 1) == 1 ? b : -1;
    }
    int peek() { return bl_bt_spp_peek(); }
    void flush() { bl_bt_spp_flush(); }
    operator bool() { return hasClient(); }
};

#endif /* BL_HAS_BT_CLASSIC */
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
#if defined(BL_HAS_WIFI)
#include "baseline_net.h"
#endif

/* ---- Wire: I2C master on the Qwiic connector ---- */
/* Wire (bus 0) and Wire1 (bus 1, on chips with a second I2C controller).
 * Bus 0 uses the original i2c_* calls, so Wire sketches also run on Baselines
 * before ABI 1.6; Wire1 needs ABI 1.6. */
#define I2C_BUFFER_LENGTH 128

class TwoWire : public Stream {
public:
    constexpr explicit TwoWire(uint8_t bus) : bus_(bus) {}
    bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
    bool begin(uint8_t address) { return false; }      // I2C target (slave) mode is not supported
    bool end();
    bool setClock(uint32_t frequency);                  // also works before begin()
    uint32_t getClock() { return freq_ ? freq_ : 100000; }
    void setTimeOut(uint16_t) {}
    void setTimeout(uint16_t) {}
    size_t setBufferSize(size_t) { return BUFFER; }
    void beginTransmission(int address);
    uint8_t endTransmission(bool sendStop = true);  // 0 ok, 2 address NACK, 4 error, 5 timeout
    uint8_t requestFrom(int address, int quantity);
    uint8_t requestFrom(int address, int quantity, int sendStop) { return requestFrom(address, quantity); }
    uint8_t requestFrom(uint8_t address, size_t quantity, bool sendStop) { return requestFrom((int)address, (int)quantity); }
    uint8_t requestFrom(uint16_t address, size_t quantity, bool sendStop) { return requestFrom((int)address, (int)quantity); }
    uint8_t requestFrom(uint8_t address, uint8_t quantity) { return requestFrom((int)address, (int)quantity); }
    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop) { return requestFrom((int)address, (int)quantity); }
    uint8_t requestFrom(uint16_t address, uint8_t quantity) { return requestFrom((int)address, (int)quantity); }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override;
    int available() override { return (int)(rxLen_ - rxPos_); }
    int read() override { return rxPos_ < rxLen_ ? rxBuf_[rxPos_++] : -1; }
    int peek() { return rxPos_ < rxLen_ ? rxBuf_[rxPos_] : -1; }
    void flush() {}

private:
    static const size_t BUFFER = I2C_BUFFER_LENGTH;
    uint8_t bus_ = 0;
    bool begun_ = false;
    int sda_ = -1;
    int scl_ = -1;
    uint32_t freq_ = 0;
    uint8_t txAddr_ = 0;
    uint8_t txBuf_[BUFFER] = {};
    size_t txLen_ = 0;
    uint8_t rxBuf_[BUFFER] = {};
    size_t rxLen_ = 0;
    size_t rxPos_ = 0;
};

#if defined(BL_PLATFORM_ESP32)
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
    const char *getChipModel() { return BL_TARGET_CHIP_NAME; }
    uint8_t getChipCores() { return 2; }
    const char *getSdkVersion() { return bl_baseline_version(); }     // the Baseline version
    uint64_t getEfuseMac();
    void restart() { bl_restart(); }
    void deepSleep(uint64_t time_us) { bl_sleep_wake(BL_WAKE_TIMER, time_us, 0, 0); bl_sleep_deep(); }
};

#endif /* BL_PLATFORM_ESP32 */
extern SerialClass Serial;
#if defined(BL_HAS_BLE)
extern BLESerialClass BLESerial;
#endif
extern TwoWire Wire;
extern TwoWire Wire1;
#if defined(BL_PLATFORM_ESP32)
extern EspClass ESP;
#endif

/* attachInterrupt, SPI, Serial1/Serial2 */
#include "baseline_io.h"
/* Arduino BLE library: BLEDevice, BLEServer, BLEScan, BLEClient, ... */
#if defined(BL_HAS_BLE)
#include "baseline_ble.h"
#endif
/* Preferences, LittleFS / File, SD / SD_MMC */
#include "baseline_storage.h"
/* PubSubClient (MQTT) */
#if defined(BL_HAS_WIFI)
#include "baseline_mqtt.h"
#endif
/* sleep, time/NTP, Ticker / hw_timer / esp_timer, tone */
#include "baseline_system.h"
/* FastLED-compatible LED strips */
#if defined(BL_PLATFORM_ESP32)
#include "baseline_fastled.h"
#endif
/* USB keyboard / mouse / gamepad / media keys / MIDI (ESP32-S3) */
#if defined(BL_HAS_USB_DEVICE)
#include "baseline_usb.h"
#endif
/* MAX17048 LiPo fuel gauge */
#include "baseline_fuelgauge.h"
#include "baseline_extras.h"
/* Teensy 4.x: IntervalTimer, elapsedMillis, the Audio library's helpers */
#ifdef BL_TARGET_TEENSY
#include "baseline_teensy.h"
#endif

#endif /* __cplusplus */
#endif /* BASELINE_ARDUINO_H */
