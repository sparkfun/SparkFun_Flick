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

#ifdef __cplusplus
}
#endif

#define BLOG_E(...) bl_console_log('E', "baseline", __VA_ARGS__)
#define BLOG_W(...) bl_console_log('W', "baseline", __VA_ARGS__)
#define BLOG_I(...) bl_console_log('I', "baseline", __VA_ARGS__)
