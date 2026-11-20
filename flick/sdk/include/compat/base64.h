/* Compatibility header: Arduino-ESP32's base64 class (base64::encode). */
#pragma once
#include "baseline.h"

class base64 {
public:
    static String encode(const uint8_t *data, size_t length)
    {
        static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String out;
        out.reserve((length + 2) / 3 * 4);
        for (size_t i = 0; i < length; i += 3) {
            uint32_t v = (uint32_t)data[i] << 16;
            if (i + 1 < length) {
                v |= (uint32_t)data[i + 1] << 8;
            }
            if (i + 2 < length) {
                v |= data[i + 2];
            }
            out += digits[(v >> 18) & 63];
            out += digits[(v >> 12) & 63];
            out += i + 1 < length ? digits[(v >> 6) & 63] : '=';
            out += i + 2 < length ? digits[v & 63] : '=';
        }
        return out;
    }
    static String encode(const String &text) { return encode((const uint8_t *)text.c_str(), text.length()); }
};
