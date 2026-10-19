#include <Arduino.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "bl_console.h"

#if !ARDUINO_USB_CDC_ON_BOOT || !ARDUINO_USB_MODE
#error "Baseline needs 'USB CDC On Boot: Enabled' and 'USB Mode: Hardware CDC and JTAG' (bsk baseline sets these)"
#endif

static int log_vprintf(const char *fmt, va_list ap)
{
    return bl_console_vprintf(fmt, ap);
}

/* HWCDC drops received bytes once its queue is full (default 256). An upload
 * DATA line is ~1 KB and arrives in one burst, so give it plenty of room. */
#define CONSOLE_RX_BUFFER 8192

void bl_console_init(void)
{
    Serial.setRxBufferSize(CONSOLE_RX_BUFFER);  // must be called before begin()
    Serial.begin(115200);       // baud is ignored on USB CDC
    esp_log_set_vprintf(log_vprintf);   // ESP-IDF logs go to the same port
}

void bl_console_write(const char *s, size_t n)
{
    Serial.write((const uint8_t *)s, n);
}

int bl_console_vprintf(const char *fmt, va_list ap)
{
    char small[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    if (n >= (int)sizeof(small)) {
        char *big = (char *)malloc(n + 1);
        if (big) {
            vsnprintf(big, n + 1, fmt, ap2);
            bl_console_write(big, n);
            free(big);
        }
    } else if (n > 0) {
        bl_console_write(small, n);
    }
    va_end(ap2);
    return n;
}

int bl_console_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_console_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

void bl_console_vlog(char level, const char *tag, const char *fmt, va_list ap)
{
    // Format the whole line first so lines from different tasks don't interleave.
    char line[256];
    int n = snprintf(line, sizeof(line), "%c (%lu) %s: ", level,
                     (unsigned long)(esp_timer_get_time() / 1000), tag);
    if (n < 0 || n >= (int)sizeof(line) - 2) {
        return;
    }
    int m = vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
    if (m < 0) {
        return;
    }
    n += m;
    if (n > (int)sizeof(line) - 2) {
        n = sizeof(line) - 2;      // truncated
    }
    line[n++] = '\n';
    bl_console_write(line, n);
}

void bl_console_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bl_console_vlog(level, tag, fmt, ap);
    va_end(ap);
}

int bl_console_read(void)
{
    return Serial.available() ? Serial.read() : -1;
}
