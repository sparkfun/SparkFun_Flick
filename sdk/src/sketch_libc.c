/*
 * sketch_libc.c - common C library calls for sketches, forwarded to Baseline
 * (sketches link without newlib). Unused functions are dropped at link time.
 */
#include <limits.h>
#include "baseline.h"

/* ---- memory ---- */
void *malloc(size_t size) { return bl_malloc(size); }
void *calloc(size_t n, size_t size) { return bl_calloc(n, size); }
void *realloc(void *ptr, size_t size) { return bl_realloc(ptr, size); }
void free(void *ptr) { bl_free(ptr); }

/* ---- formatted output ---- */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) { return bl_vsnprintf(buf, size, fmt, ap); }

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_vsnprintf(buf, INT_MAX, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) { return bl_vprintf(fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    bl_console_write((const uint8_t *)s, strlen(s));
    bl_console_write((const uint8_t *)"\n", 1);
    return 1;
}

int putchar(int c)
{
    uint8_t ch = (uint8_t)c;
    bl_console_write(&ch, 1);
    return c;
}

/* ---- number parsing ---- */
static int digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = (*p++ == '-');
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    unsigned long v = 0;
    const char *start = p;
    for (int d; (d = digit_value(*p)) < base; p++) {
        v = v * base + d;
    }
    if (end) *end = (char *)(p == start ? s : p);
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base) { return (long)strtoul(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* Decimal floating point: [sign] digits [. digits] [e [sign] digits]. */
double strtod(const char *s, char **end)
{
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    double sign = 1.0;
    if (*p == '+' || *p == '-') sign = (*p++ == '-') ? -1.0 : 1.0;
    double v = 0.0;
    bool any = false;
    for (; *p >= '0' && *p <= '9'; p++, any = true) v = v * 10.0 + (*p - '0');
    if (*p == '.') {
        double scale = 0.1;
        for (p++; *p >= '0' && *p <= '9'; p++, any = true, scale *= 0.1) v += (*p - '0') * scale;
    }
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int esign = 1, e = 0;
        if (*q == '+' || *q == '-') esign = (*q++ == '-') ? -1 : 1;
        if (*q >= '0' && *q <= '9') {
            for (; *q >= '0' && *q <= '9'; q++) e = e * 10 + (*q - '0');
            for (; e > 0; e--) v = esign > 0 ? v * 10.0 : v / 10.0;
            p = q;
        }
    }
    if (end) *end = (char *)(any ? p : s);
    return sign * v;
}

double atof(const char *s) { return strtod(s, NULL); }
float strtof(const char *s, char **end) { return (float)strtod(s, end); }

