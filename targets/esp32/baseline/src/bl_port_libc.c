/*
 * bl_port_libc.c - classic ESP32: keep some newlib functions out of IRAM.
 *
 * Arduino-ESP32's classic-ESP32 libraries are built with the PSRAM cache
 * workaround, which places most of newlib in IRAM. With WiFi, BLE and
 * Bluetooth Classic all linked, IRAM is full. target.json links Baseline with
 * -Wl,--wrap=<name> for the functions below, so every caller (FATFS, WiFi's
 * wpa_supplicant, mbedtls, lwIP...) uses these flash-resident versions
 * instead. None of them is called with the flash cache disabled.
 *
 * Time: Baseline never sets a time zone (the SDK does time zones in the
 * sketch), so localtime is UTC, exactly as newlib would compute it here.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ---------------- time (UTC) ---------------- */

static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct tm *__wrap_gmtime_r(const time_t *tp, struct tm *tm)
{
    if (!tp || !tm) {
        return NULL;
    }
    int64_t t = *tp;
    int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    tm->tm_wday = (int)((days % 7 + 11) % 7);
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    int64_t y = yoe + era * 400 + (m <= 2);
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    return tm;
}

struct tm *__wrap_localtime_r(const time_t *tp, struct tm *tm)
{
    return __wrap_gmtime_r(tp, tm);
}

time_t __wrap_mktime(struct tm *tm)
{
    if (!tm) {
        return (time_t)-1;
    }
    int64_t mon = tm->tm_mon, year = tm->tm_year + 1900LL;
    year += mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        year--;
    }
    time_t t = (time_t)(days_from_civil(year, (int)mon + 1, 1) * 86400 + (int64_t)(tm->tm_mday - 1) * 86400 +
                        (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 + tm->tm_sec);
    __wrap_gmtime_r(&t, tm);    /* normalize the fields, like mktime */
    return t;
}

/* ---------------- strtol / strtoul / strtok_r ---------------- */

static int digit_of(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* Shared parser: returns the magnitude, sets *neg, *end, *overflow. */
static unsigned long parse_ul(const char *s, char **end, int base, bool *neg, bool *overflow)
{
    const char *p = s;
    *neg = false;
    *overflow = false;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '+' || *p == '-') *neg = (*p++ == '-');
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_of(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        if (end) *end = (char *)s;
        errno = EINVAL;
        return 0;
    }
    unsigned long v = 0;
    const char *start = p;
    for (int d; (d = digit_of(*p)) < base; p++) {
        if (v > (ULONG_MAX - (unsigned)d) / (unsigned)base) {
            *overflow = true;
        } else {
            v = v * base + d;
        }
    }
    if (end) *end = (char *)(p == start ? s : p);
    return v;
}

unsigned long __wrap_strtoul(const char *s, char **end, int base)
{
    bool neg, overflow;
    unsigned long v = parse_ul(s, end, base, &neg, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return neg ? (unsigned long)(-(long)v) : v;
}

long __wrap_strtol(const char *s, char **end, int base)
{
    bool neg, overflow;
    unsigned long v = parse_ul(s, end, base, &neg, &overflow);
    if (!neg && (overflow || v > (unsigned long)LONG_MAX)) {
        errno = ERANGE;
        return LONG_MAX;
    }
    if (neg && (overflow || v > (unsigned long)LONG_MAX + 1UL)) {
        errno = ERANGE;
        return LONG_MIN;
    }
    return neg ? (long)(0UL - v) : (long)v;
}

char *__wrap_strtok_r(char *s, const char *delim, char **save)
{
    if (!s) {
        s = *save;
    }
    if (!s) {
        return NULL;
    }
    s += strspn(s, delim);
    if (!*s) {
        *save = NULL;
        return NULL;
    }
    char *tok = s;
    s += strcspn(s, delim);
    if (*s) {
        *s++ = '\0';
        *save = s;
    } else {
        *save = NULL;
    }
    return tok;
}

/* ---------------- libc internals that pull IRAM members back in ----------------
 * --wrap also redirects references made inside libc itself: atoi() calls
 * _strtol_r, sscanf() calls _strtoul_r, strtok()/strsep() call __strtok_r,
 * itoa() calls __utoa, tzset() calls _getenv_r. */

struct _reent;

long __wrap__strtol_r(struct _reent *r, const char *s, char **end, int base)
{
    (void)r;
    return __wrap_strtol(s, end, base);
}

unsigned long __wrap__strtoul_r(struct _reent *r, const char *s, char **end, int base)
{
    (void)r;
    return __wrap_strtoul(s, end, base);
}

/* skip_leading = 1 for strtok, 0 for strsep (empty tokens are returned). */
char *__wrap___strtok_r(char *s, const char *delim, char **save, int skip_leading)
{
    if (skip_leading) {
        return __wrap_strtok_r(s, delim, save);
    }
    if (!s) {
        s = *save;
    }
    if (!s) {
        return NULL;
    }
    char *tok = s;
    s += strcspn(s, delim);
    if (*s) {
        *s++ = '\0';
        *save = s;
    } else {
        *save = NULL;
    }
    return tok;
}

/* ---------------- itoa / utoa ---------------- */

char *__wrap_utoa(unsigned value, char *str, int base)
{
    if (base < 2 || base > 36) {
        str[0] = '\0';
        return NULL;
    }
    char tmp[33];
    int n = 0;
    do {
        unsigned d = value % (unsigned)base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= (unsigned)base;
    } while (value);
    for (int i = 0; i < n; i++) {
        str[i] = tmp[n - 1 - i];
    }
    str[n] = '\0';
    return str;
}

char *__wrap_itoa(int value, char *str, int base)
{
    if (base == 10 && value < 0) {
        str[0] = '-';
        __wrap_utoa(0u - (unsigned)value, str + 1, base);
        return str;
    }
    return __wrap_utoa((unsigned)value, str, base);
}

char *__wrap___utoa(unsigned value, char *str, int base) { return __wrap_utoa(value, str, base); }
char *__wrap___itoa(int value, char *str, int base) { return __wrap_itoa(value, str, base); }

/* ---------------- rand (newlib's generator, one shared seed) ---------------- */

static uint64_t s_rand_next = 1;

void __wrap_srand(unsigned seed)
{
    s_rand_next = seed;
}

int __wrap_rand(void)
{
    s_rand_next = s_rand_next * 6364136223846793005ULL + 1;
    return (int)((s_rand_next >> 32) & 0x7FFFFFFF);
}

/* ---------------- getenv ---------------- */

extern char **environ;

char *__wrap__getenv_r(struct _reent *r, const char *name)
{
    (void)r;
    size_t n = strlen(name);
    for (char **e = environ; e && *e; e++) {
        if (strncmp(*e, name, n) == 0 && (*e)[n] == '=') {
            return *e + n + 1;
        }
    }
    return NULL;
}

char *__wrap_getenv(const char *name)
{
    return __wrap__getenv_r(NULL, name);
}

/* ---------------- strcasestr / strlcat ---------------- */

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

char *__wrap_strcasestr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && lower((unsigned char)hay[i]) == lower((unsigned char)needle[i])) {
            i++;
        }
        if (i == n) {
            return (char *)hay;
        }
    }
    return n ? NULL : (char *)hay;
}

size_t __wrap_strlcat(char *dst, const char *src, size_t size)
{
    size_t d = strnlen(dst, size);
    size_t s = strlen(src);
    if (d == size) {
        return size + s;
    }
    size_t copy = s < size - d - 1 ? s : size - d - 1;
    memcpy(dst + d, src, copy);
    dst[d + copy] = '\0';
    return d + s;
}

/* ---------------- more string helpers ---------------- */

#include <stdlib.h>

int __wrap_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);
        if (d || !*a) {
            return d;
        }
    }
    return 0;
}

int __wrap_strcasecmp(const char *a, const char *b)
{
    return __wrap_strncasecmp(a, b, (size_t)-1);
}

char *__wrap_strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *d = (char *)malloc(len + 1);
    if (d) {
        memcpy(d, s, len);
        d[len] = '\0';
    }
    return d;
}

char *__wrap_strdup(const char *s) { return __wrap_strndup(s, (size_t)-1); }
char *__wrap__strdup_r(struct _reent *r, const char *s) { (void)r; return __wrap_strdup(s); }
char *__wrap__strndup_r(struct _reent *r, const char *s, size_t n) { (void)r; return __wrap_strndup(s, n); }

char *__wrap_strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        if (strncmp(hay, needle, n) == 0) {
            return (char *)hay;
        }
    }
    return n ? NULL : (char *)hay;
}

char *__wrap_strrchr(const char *s, int c)
{
    const char *last = NULL;
    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);
    return (char *)last;
}

void *__wrap_memccpy(void *dst, const void *src, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        if ((*d++ = *s++) == (unsigned char)c) {
            return d;
        }
    }
    return NULL;
}

int __wrap_atoi(const char *s) { return (int)__wrap_strtol(s, NULL, 10); }
long __wrap_atol(const char *s) { return __wrap_strtol(s, NULL, 10); }

/* scanf's %[...] character class (newlib's __sccl, unchanged in behavior). */
unsigned char *__wrap___sccl(char *tab, unsigned char *fmt)
{
    int c, n, v;
    c = *fmt++;
    if (c == '^') {
        v = 1;
        c = *fmt++;
    } else {
        v = 0;
    }
    memset(tab, v, 256);
    if (c == 0) {
        return fmt - 1;
    }
    v = 1 - v;
    for (;;) {
        tab[c] = (char)v;
    doswitch:
        n = *fmt++;
        switch (n) {
        case 0:
            return fmt - 1;
        case '-':
            n = *fmt;
            if (n == ']' || n < c) {
                c = '-';
                break;
            }
            fmt++;
            do {
                tab[++c] = (char)v;
            } while (c < n);
            goto doswitch;
        case ']':
            return fmt;
        default:
            c = n;
            break;
        }
    }
}
