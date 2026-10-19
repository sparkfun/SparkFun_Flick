/* sketch_arduino.cpp - out-of-line parts of baseline_arduino.h. */
#include "baseline.h"

SerialClass Serial;
BLESerialClass BLESerial;
TwoWire Wire;
EspClass ESP;

/* ---- Print ---- */

size_t Print::printNumber(unsigned long long n, int base)
{
    char buf[8 * sizeof(n) + 1];
    char *p = buf + sizeof(buf);
    if (base < 2) {
        base = 10;
    }
    do {
        int digit = (int)(n % (unsigned)base);
        *--p = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        n /= (unsigned)base;
    } while (n);
    return write((const uint8_t *)p, buf + sizeof(buf) - p);
}

size_t Print::printSigned(long long n)
{
    if (n < 0) {
        return print('-') + printNumber(0ULL - (unsigned long long)n, DEC);
    }
    return printNumber((unsigned long long)n, DEC);
}

size_t Print::print(double n, int digits)
{
    char buf[40];
    int len = bl_snprintf(buf, sizeof(buf), "%.*f", digits, n);
    return write((const uint8_t *)buf, len < (int)sizeof(buf) ? len : sizeof(buf) - 1);
}

size_t Print::printf(const char *fmt, ...)
{
    char small[128];
    va_list ap;
    va_start(ap, fmt);
    int len = bl_vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (len < 0) {
        return 0;
    }
    if (len < (int)sizeof(small)) {
        return write((const uint8_t *)small, len);
    }
    char *big = (char *)bl_malloc(len + 1);
    if (!big) {
        return 0;
    }
    va_start(ap, fmt);
    bl_vsnprintf(big, len + 1, fmt, ap);
    va_end(ap);
    size_t n = write((const uint8_t *)big, len);
    bl_free(big);
    return n;
}

/* ---- Stream ---- */

int Stream::timedRead()
{
    uint32_t start = millis();
    do {
        int c = read();
        if (c >= 0) {
            return c;
        }
        delay(1);
    } while (millis() - start < timeout_);
    return -1;
}

size_t Stream::readBytesUntil(char terminator, char *buffer, size_t length)
{
    size_t n = 0;
    while (n < length) {
        int c = timedRead();
        if (c < 0 || c == terminator) {
            break;
        }
        buffer[n++] = (char)c;
    }
    return n;
}

String Stream::readStringUntil(char terminator)
{
    String s;
    int c;
    while ((c = timedRead()) >= 0 && c != terminator) {
        s += (char)c;
    }
    return s;
}

String Stream::readString()
{
    String s;
    int c;
    while ((c = timedRead()) >= 0) {
        s += (char)c;
    }
    return s;
}

/* ---- IPAddress ---- */

bool IPAddress::fromString(const char *text)
{
    uint8_t b[4];
    const char *p = text;
    for (int i = 0; i < 4; i++) {
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p || v > 255 || (i < 3 && *end != '.') || (i == 3 && *end != '\0')) {
            return false;
        }
        b[i] = (uint8_t)v;
        p = end + 1;
    }
    memcpy(bytes_, b, 4);
    return true;
}

String IPAddress::toString() const
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
    return String(buf);
}

size_t IPAddress::printTo(Print &p) const
{
    return p.print(toString());
}

/* ---- ESP ---- */

uint64_t EspClass::getEfuseMac()
{
    uint8_t m[6];
    bl_wifi_mac(m);
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) {
        v = (v << 8) | m[i];     // same byte order as Arduino-ESP32
    }
    return v;
}

/* ---- Wire ---- */

void TwoWire::beginTransmission(int address)
{
    txAddr_ = (uint8_t)address;
    txLen_ = 0;
}

size_t TwoWire::write(const uint8_t *buf, size_t size)
{
    size_t n = size < BUFFER - txLen_ ? size : BUFFER - txLen_;
    memcpy(txBuf_ + txLen_, buf, n);
    txLen_ += n;
    return n;
}

uint8_t TwoWire::endTransmission(bool sendStop)
{
    int r = bl_i2c_write(txAddr_, txBuf_, txLen_, sendStop);
    txLen_ = 0;
    switch (r) {
    case BL_OK:          return 0;
    case BL_ERR_NACK:    return 2;
    case BL_ERR_TIMEOUT: return 5;
    default:             return 4;
    }
}

uint8_t TwoWire::requestFrom(int address, int quantity)
{
    if (quantity < 0) {
        quantity = 0;
    }
    if ((size_t)quantity > BUFFER) {
        quantity = BUFFER;
    }
    int n = bl_i2c_read((uint8_t)address, rxBuf_, (size_t)quantity);
    rxLen_ = n > 0 ? (size_t)n : 0;
    rxPos_ = 0;
    return (uint8_t)rxLen_;
}
