#include <Arduino.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "bl_console.h"
#include "bl_port.h"

#if BL_PORT_CONSOLE_USB && (!ARDUINO_USB_CDC_ON_BOOT || !ARDUINO_USB_MODE)
#error "Baseline needs 'USB CDC On Boot: Enabled' and 'USB Mode: Hardware CDC and JTAG' (flick baseline sets these)"
#endif

static int log_vprintf(const char *fmt, va_list ap)
{
    return bl_console_vprintf(fmt, ap);
}

/* The console drops received bytes once its queue is full (HWCDC default 256).
 * An upload DATA line is ~1 KB and arrives in one burst, so give it plenty of
 * room. On the classic ESP32 the console is a UART, whose driver keeps this
 * buffer in internal RAM, where every KB counts with Bluetooth and WiFi on:
 * there it holds the loader's window of 4 lines (loader_proto.c BAUD_WINDOW)
 * with a line to spare. */
#if CONFIG_IDF_TARGET_ESP32
#define CONSOLE_RX_BUFFER 5120
#else
#define CONSOLE_RX_BUFFER 8192
#endif

/* UART consoles (USB-serial chip boards) run at the chip's BL_PORT_CONSOLE_BAUD. */
#ifndef BL_PORT_CONSOLE_BAUD
#define BL_PORT_CONSOLE_BAUD 115200
#endif

static Stream *s_con = &Serial;

void bl_console_init(void)
{
    Serial.setRxBufferSize(CONSOLE_RX_BUFFER);  // must be called before begin()
    Serial.begin(BL_PORT_CONSOLE_BAUD);     // UART console; the baud rate is ignored on USB CDC
    esp_log_set_vprintf(log_vprintf);   // ESP-IDF logs go to the same port
}

void bl_console_use(Stream *s)
{
    s_con = s ? s : &Serial;
}

static unsigned long s_baud = BL_PORT_CONSOLE_BAUD;

int bl_console_is_uart(void)
{
    return !BL_PORT_CONSOLE_USB;
}

unsigned long bl_console_baud(void)
{
    return s_baud;
}

unsigned long bl_console_default_baud(void)
{
    return BL_PORT_CONSOLE_BAUD;
}

void bl_console_set_baud(unsigned long baud)
{
#if !BL_PORT_CONSOLE_USB
    Serial.flush();                 // the last reply leaves at the old rate
    Serial.updateBaudRate(baud);
    s_baud = baud;
#else
    (void)baud;
#endif
}

void bl_console_write(const char *s, size_t n)
{
    s_con->write((const uint8_t *)s, n);
}

void bl_console_flush(void)
{
    s_con->flush();
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
    return s_con->available() ? s_con->read() : -1;
}

size_t bl_console_read_buf(char *buf, size_t max)
{
    int avail = s_con->available();
    if (avail <= 0) {
        return 0;
    }
    size_t n = (size_t)avail < max ? (size_t)avail : max;
#if !BL_PORT_CONSOLE_USB
    if (s_con == &Serial) {
        // One UART driver call for the lot: byte by byte, the driver's lock and
        // queue cost more than the bytes at 921600 (uploads ran at ~20 KB/s).
        return Serial.read((uint8_t *)buf, n);
    }
#endif
    size_t got = 0;
    while (got < n) {
        int c = s_con->read();
        if (c < 0) {
            break;
        }
        buf[got++] = (char)c;
    }
    return got;
}
