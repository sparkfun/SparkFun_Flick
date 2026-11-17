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


/* errno for the sketch (libm and other newlib pieces set it). One per sketch:
 * the sketch is a single task. */
int *__errno(void)
{
    static int e;
    return &e;
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t d = strnlen(dst, size);
    return d == size ? size + strlen(src) : d + strlcpy(dst + d, src, size - d);
}

/* ---- FILE output: stdout and stderr are the console ----
 * Library code sometimes writes diagnostics with fputs(s, stderr) or
 * fprintf(stderr, ...). Every stream goes to the console. */
#include <stdarg.h>
#include <sys/reent.h>

static struct _reent s_reent;       /* stdin/stdout/stderr are NULL: only ever passed back to us */

struct _reent *__getreent(void)
{
    return &s_reent;
}

int fputs(const char *s, FILE *f)
{
    (void)f;
    return bl_console_write((const uint8_t *)s, strlen(s));
}

int fputc(int c, FILE *f)
{
    (void)f;
    return putchar(c);
}

size_t fwrite(const void *data, size_t size, size_t count, FILE *f)
{
    (void)f;
    bl_console_write((const uint8_t *)data, size * count);
    return count;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    (void)f;
    return vprintf(fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;
}

#if defined(FLICK_ARCH_IMXRT)
/* Teensy sketches link newlib. A library that names stderr pulls in newlib's
 * stream table, which reaches fclose() and from there fflush.o (__sflush_r),
 * an object that also defines fflush: a duplicate of the one above. Defining
 * its other two symbols here keeps fflush.o out. No stream is ever a real
 * newlib FILE, so there is nothing to flush. */
int _fflush_r(struct _reent *r, FILE *f)
{
    (void)r;
    (void)f;
    return 0;
}

int __sflush_r(struct _reent *r, FILE *f)
{
    (void)r;
    (void)f;
    return 0;
}
#endif

/* ---- pthread locks: the sketch is a single task ----
 * Parts of libgcc/libstdc++ guard shared state with these; with one task
 * there is nothing to lock. */
#include <pthread.h>

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) { (void)m; (void)attr; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }

#define SKETCH_PTHREAD_KEYS 8
static const void *s_key_values[SKETCH_PTHREAD_KEYS];
static int s_keys_used;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    (void)destructor;
    if (s_keys_used >= SKETCH_PTHREAD_KEYS) {
        return 11;  /* EAGAIN */
    }
    *key = (pthread_key_t)s_keys_used++;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    return (unsigned)key < SKETCH_PTHREAD_KEYS ? (void *)s_key_values[key] : NULL;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if ((unsigned)key >= SKETCH_PTHREAD_KEYS) {
        return 22;  /* EINVAL */
    }
    s_key_values[key] = value;
    return 0;
}

/* More of the same, needed on chips without atomic instructions (ESP32-C3),
 * where libstdc++ guards its one-time initialization with locks. */
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type) { (void)a; (void)type; return 0; }
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)c; (void)a; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }   /* never contended */
int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }

int pthread_once(pthread_once_t *once, void (*init)(void))
{
    if (!once->init_executed) {
        once->init_executed = 1;
        init();
    }
    return 0;
}

/* A sketch never exits, so exit handlers never run. */
int atexit(void (*fn)(void))
{
    (void)fn;
    return 0;
}
