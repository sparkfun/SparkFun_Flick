/* bl_str.h - copying Arduino Strings into C buffers. */
#pragma once

#include <Arduino.h>

/* Arduino-ESP32's String::c_str() returns NULL for a String that never held
 * text (Bluedroid BLE hands those back, for example), so never pass c_str()
 * straight to strlcpy. Returns strlen of the source, like strlcpy. */
static inline size_t bl_copy_str(char *dst, size_t size, const String &s)
{
    const char *p = s.c_str();
    return strlcpy(dst, p ? p : "", size);
}
