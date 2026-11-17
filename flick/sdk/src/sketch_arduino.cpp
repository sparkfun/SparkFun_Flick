/* sketch_arduino.cpp - out-of-line parts of baseline_arduino.h. */
#include "baseline.h"

SerialClass Serial;
#if defined(BL_HAS_BLE)
BLESerialClass BLESerial;
#endif
TwoWire Wire(0);
TwoWire Wire1(1);
#if defined(BL_PLATFORM_ESP32)
EspClass ESP;
#endif

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

size_t Stream::readBytes(char *buffer, size_t length)
{
    size_t n = 0;
    while (n < length) {
        int c = timedRead();
        if (c < 0) {
            break;
        }
        buffer[n++] = (char)c;
    }
    return n;
}

bool Stream::findUntil(const char *target, const char *terminator)
{
    size_t tlen = strlen(target), tpos = 0, ulen = terminator ? strlen(terminator) : 0, upos = 0;
    if (!tlen) {
        return true;
    }
    for (int c; (c = timedRead()) >= 0;) {
        tpos = c == target[tpos] ? tpos + 1 : (c == target[0] ? 1 : 0);
        if (tpos == tlen) {
            return true;
        }
        if (ulen) {
            upos = c == terminator[upos] ? upos + 1 : (c == terminator[0] ? 1 : 0);
            if (upos == ulen) {
                return false;
            }
        }
    }
    return false;
}

bool Stream::find(const char *target)
{
    return findUntil(target, nullptr);
}

/* Skip to the first digit or '-' (or '.', for floats); waits up to the timeout. */
static int peek_number_start(Stream &s, bool allow_dot)
{
    unsigned long start = millis();
    for (;;) {
        int c = s.peek();
        if (c < 0) {
            if (millis() - start >= s.getTimeout()) {
                return -1;
            }
            delay(1);
            continue;
        }
        if (c == '-' || (c >= '0' && c <= '9') || (allow_dot && c == '.')) {
            return c;
        }
        s.read();
    }
}

long Stream::parseInt()
{
    if (peek_number_start(*this, false) < 0) {
        return 0;
    }
    bool neg = false;
    long v = 0;
    for (int c; (c = peek()) >= 0;) {
        if (c == '-' && !neg && v == 0) {
            neg = true;
        } else if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
        } else {
            break;
        }
        read();
    }
    return neg ? -v : v;
}

float Stream::parseFloat()
{
    if (peek_number_start(*this, true) < 0) {
        return 0;
    }
    bool neg = false, frac = false;
    float v = 0, scale = 1;
    for (int c; (c = peek()) >= 0;) {
        if (c == '-' && !neg && v == 0 && !frac) {
            neg = true;
        } else if (c == '.' && !frac) {
            frac = true;
        } else if (c >= '0' && c <= '9') {
            if (frac) {
                scale /= 10;
                v += (c - '0') * scale;
            } else {
                v = v * 10 + (c - '0');
            }
        } else {
            break;
        }
        read();
    }
    return neg ? -v : v;
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

#if defined(BL_PLATFORM_ESP32)
/* ---- ESP ---- */

uint64_t EspClass::getEfuseMac()
{
    uint8_t m[6] = {0};
    BL_CALL(wifi_mac)(m);        /* the table entry: boards without WiFi (P4) answer with an error, so 0 */
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) {
        v = (v << 8) | m[i];     // same byte order as Arduino-ESP32
    }
    return v;
}

#endif /* BL_PLATFORM_ESP32 */
/* ---- Wire ---- */

bool TwoWire::begin(int sda, int scl, uint32_t frequency)
{
    if (frequency) {
        freq_ = frequency;
    }
    int r = bus_ == 0 ? bl_i2c_begin(sda, scl, freq_) : bl_i2cx_begin(bus_, sda, scl, freq_);
    begun_ = r == BL_OK;
    if (begun_) {
        sda_ = sda;
        scl_ = scl;
    }
    return begun_;
}

bool TwoWire::end()
{
    if (bus_ != 0 || BL_HAS(i2cx_end)) {
        BL->i2cx_end(bus_);     /* guarded: needs no newer Baseline */
    }
    begun_ = false;
    return true;
}

bool TwoWire::setClock(uint32_t frequency)
{
    freq_ = frequency;
    return begun_ ? begin(sda_, scl_, frequency) : true;   // restart the bus at the new speed
}

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
    int r = bus_ == 0 ? bl_i2c_write(txAddr_, txBuf_, txLen_, sendStop)
                      : bl_i2cx_write(bus_, txAddr_, txBuf_, txLen_, sendStop);
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
    int n = bus_ == 0 ? bl_i2c_read((uint8_t)address, rxBuf_, (size_t)quantity)
                      : bl_i2cx_read(bus_, (uint8_t)address, rxBuf_, (size_t)quantity);
    rxLen_ = n > 0 ? (size_t)n : 0;
    rxPos_ = 0;
    return (uint8_t)rxLen_;
}
