/* Console on the native USB port (Arduino HWCDC `Serial`). Shared by Baseline
 * logging, sketch printf/log, and the sketch loader protocol. */
#pragma once

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void bl_console_init(void);
void bl_console_write(const char *s, size_t n);
int  bl_console_vprintf(const char *fmt, va_list ap);
int  bl_console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void bl_console_vlog(char level, const char *tag, const char *fmt, va_list ap);
void bl_console_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int  bl_console_read(void);     /* next received byte, or -1 */
size_t bl_console_read_buf(char *buf, size_t max);   /* what has arrived, up to max bytes (0: none) */
void bl_console_flush(void);

/* A UART console (boards with a USB-serial chip) runs at BL_PORT_CONSOLE_BAUD;
 * the loader's BAUD command raises it for the length of an upload. On a USB
 * console the rate means nothing: bl_console_is_uart() is 0 there. */
int           bl_console_is_uart(void);
unsigned long bl_console_baud(void);
unsigned long bl_console_default_baud(void);
void          bl_console_set_baud(unsigned long baud);

/* USB device mode (bl_impl_usb.cpp): switch the S3 to TinyUSB at boot when a
 * sketch asked for USB devices; forget that request when a new sketch is installed. */
void bl_usb_boot(int safe_mode);
void bl_usb_forget(void);

#ifdef __cplusplus
}
class Stream;
/* Move the console to another stream (USB device mode uses a TinyUSB CDC port). */
void bl_console_use(Stream *s);
#endif

#define BLOG_E(...) bl_console_log('E', "baseline", __VA_ARGS__)
#define BLOG_W(...) bl_console_log('W', "baseline", __VA_ARGS__)
#define BLOG_I(...) bl_console_log('I', "baseline", __VA_ARGS__)
