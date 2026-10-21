/*
 * sketch_time.c - the C time functions for sketches: time, gettimeofday,
 * gmtime, localtime, mktime, timegm, strftime, setenv("TZ")/tzset, plus the
 * time zone behind configTime()/configTzTime().
 *
 * Baseline keeps the UTC clock (and runs NTP); everything that depends on the
 * time zone is done here, in the sketch. Time zones are POSIX TZ strings, e.g.
 * "MST7MDT,M3.2.0,M11.1.0" or "CET-1CEST,M3.5.0,M10.5.0/3" or "<+0530>-5:30".
 * Unused functions are dropped at link time.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "baseline.h"

/* ---------------- calendar arithmetic (proleptic Gregorian) ---------------- */

static int64_t days_from_civil(int64_t y, int m, int d)     /* m 1-12 */
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool is_leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int64_t y, int m)      /* m 1-12 */
{
    static const uint8_t k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap(y) ? 29 : k[m - 1];
}

static void fill_tm(int64_t t, struct tm *tm)
{
    int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    tm->tm_wday = (int)((days % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
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
}

/* Seconds since 1970 for a broken-down UTC time; out-of-range fields roll over. */
static int64_t tm_to_seconds(const struct tm *tm)
{
    int64_t mon = tm->tm_mon, year = tm->tm_year + 1900LL;
    year += mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        year--;
    }
    return days_from_civil(year, (int)mon + 1, 1) * 86400 + (int64_t)(tm->tm_mday - 1) * 86400 +
           (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 + tm->tm_sec;
}

/* ---------------- POSIX TZ ---------------- */

typedef struct {
    char kind;          /* 'M' month/week/day, 'J' Julian 1-365 (no leap day), 'D' day 0-365 */
    int month, week, day;
    int32_t secs;       /* local time of the change, seconds after midnight */
} tz_rule_t;

static struct {
    char std_name[12], dst_name[12];
    int32_t std_off;    /* seconds EAST of UTC (POSIX strings use west) */
    int32_t dst_off;
    bool has_dst;
    tz_rule_t start, end;
    char text[64];      /* the TZ string, for getenv("TZ") */
} s_tz = { "UTC", "UTC", 0, 0, false, { 'M', 3, 2, 0, 7200 }, { 'M', 11, 1, 0, 7200 }, "UTC0" };

static const char *parse_name(const char *p, char *out, size_t size)
{
    size_t n = 0;
    if (*p == '<') {        /* quoted: <+0530> */
        for (p++; *p && *p != '>'; p++) {
            if (n + 1 < size) out[n++] = *p;
        }
        if (*p == '>') p++;
    } else {
        for (; (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'); p++) {
            if (n + 1 < size) out[n++] = *p;
        }
    }
    out[n] = '\0';
    return p;
}

static const char *parse_hms(const char *p, int32_t *secs)  /* [+-]hh[:mm[:ss]] */
{
    int sign = 1;
    if (*p == '+' || *p == '-') sign = (*p++ == '-') ? -1 : 1;
    int32_t h = 0, m = 0, s = 0;
    while (*p >= '0' && *p <= '9') h = h * 10 + (*p++ - '0');
    if (*p == ':') {
        for (p++; *p >= '0' && *p <= '9'; p++) m = m * 10 + (*p - '0');
        if (*p == ':') {
            for (p++; *p >= '0' && *p <= '9'; p++) s = s * 10 + (*p - '0');
        }
    }
    *secs = sign * (h * 3600 + m * 60 + s);
    return p;
}

static const char *parse_num(const char *p, int *v)
{
    *v = 0;
    while (*p >= '0' && *p <= '9') *v = *v * 10 + (*p++ - '0');
    return p;
}

static const char *parse_rule(const char *p, tz_rule_t *r)
{
    if (*p == 'M') {
        r->kind = 'M';
        p = parse_num(p + 1, &r->month);
        if (*p == '.') p = parse_num(p + 1, &r->week);
        if (*p == '.') p = parse_num(p + 1, &r->day);
    } else if (*p == 'J') {
        r->kind = 'J';
        p = parse_num(p + 1, &r->day);
    } else {
        r->kind = 'D';
        p = parse_num(p, &r->day);
    }
    r->secs = 7200;     /* default 02:00 */
    if (*p == '/') p = parse_hms(p + 1, &r->secs);
    return p;
}

int bl_sdk_tz_set(const char *tz)
{
    if (!tz || !*tz) tz = "UTC0";
    if (*tz == ':') tz++;
    strncpy(s_tz.text, tz, sizeof(s_tz.text) - 1);
    s_tz.text[sizeof(s_tz.text) - 1] = '\0';
    const char *p = parse_name(tz, s_tz.std_name, sizeof(s_tz.std_name));
    int32_t west;
    p = parse_hms(p, &west);
    s_tz.std_off = -west;
    s_tz.has_dst = false;
    s_tz.dst_name[0] = '\0';
    if (*p && *p != ',') {
        p = parse_name(p, s_tz.dst_name, sizeof(s_tz.dst_name));
        s_tz.has_dst = s_tz.dst_name[0] != '\0';
        s_tz.dst_off = s_tz.std_off + 3600;
        if ((*p >= '0' && *p <= '9') || *p == '+' || *p == '-') {
            p = parse_hms(p, &west);
            s_tz.dst_off = -west;
        }
    }
    /* Rules default to the US ones, like newlib. */
    s_tz.start = (tz_rule_t){ 'M', 3, 2, 0, 7200 };
    s_tz.end = (tz_rule_t){ 'M', 11, 1, 0, 7200 };
    if (*p == ',') {
        p = parse_rule(p + 1, &s_tz.start);
        if (*p == ',') parse_rule(p + 1, &s_tz.end);
    }
    return 0;
}

/* configTime(gmtOffset, daylightOffset): fixed offsets, US DST rules if DST is set. */
void bl_sdk_tz_set_offsets(long gmt_offset_sec, int dst_offset_sec)
{
    s_tz.std_off = (int32_t)gmt_offset_sec;
    s_tz.dst_off = (int32_t)(gmt_offset_sec + dst_offset_sec);
    s_tz.has_dst = dst_offset_sec != 0;
    strcpy(s_tz.std_name, "UTC");
    strcpy(s_tz.dst_name, "DST");
    s_tz.start = (tz_rule_t){ 'M', 3, 2, 0, 7200 };
    s_tz.end = (tz_rule_t){ 'M', 11, 1, 0, 7200 };
    snprintf(s_tz.text, sizeof(s_tz.text), "UTC%+ld", -gmt_offset_sec / 3600);
}

/* Local seconds-of-year epoch: when `r` happens in year y, as a local timestamp. */
static int64_t rule_local_time(const tz_rule_t *r, int64_t y)
{
    int64_t day;
    if (r->kind == 'M') {
        int64_t first = days_from_civil(y, r->month, 1);
        int wday_first = (int)((first % 7 + 11) % 7);
        int d = 1 + (r->day - wday_first + 7) % 7 + (r->week - 1) * 7;
        while (d > days_in_month(y, r->month)) d -= 7;   /* week 5 = last */
        day = first + d - 1;
    } else if (r->kind == 'J') {
        day = days_from_civil(y, 1, 1) + r->day - 1 + (is_leap(y) && r->day >= 60 ? 1 : 0);
    } else {
        day = days_from_civil(y, 1, 1) + r->day;
    }
    return day * 86400 + r->secs;
}

static bool in_dst(int64_t utc)
{
    if (!s_tz.has_dst) return false;
    struct tm tm;
    fill_tm(utc + s_tz.std_off, &tm);
    int64_t y = tm.tm_year + 1900LL;
    int64_t start = rule_local_time(&s_tz.start, y) - s_tz.std_off;    /* to UTC */
    int64_t end = rule_local_time(&s_tz.end, y) - s_tz.dst_off;
    return start < end ? (utc >= start && utc < end) : !(utc >= end && utc < start);
}

/* ---------------- the C library calls ---------------- */

time_t time(time_t *t)
{
    int64_t sec = 0;
    bl_time_get(&sec, NULL);
    if (t) *t = (time_t)sec;
    return (time_t)sec;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    int64_t sec = 0;
    int32_t usec = 0;
    bl_time_get(&sec, &usec);
    if (tv) {
        tv->tv_sec = (time_t)sec;
        tv->tv_usec = usec;
    }
    return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    return tv ? (bl_time_set(tv->tv_sec, (int32_t)tv->tv_usec) == BL_OK ? 0 : -1) : 0;
}

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    if (!t || !out) return NULL;
    fill_tm(*t, out);
    return out;
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    if (!t || !out) return NULL;
    bool dst = in_dst(*t);
    fill_tm(*t + (dst ? s_tz.dst_off : s_tz.std_off), out);
    out->tm_isdst = dst;
    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm tm;
    return gmtime_r(t, &tm);
}

struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    return localtime_r(t, &tm);
}

time_t timegm(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    time_t t = (time_t)tm_to_seconds(tm);
    fill_tm(t, tm);     /* normalize */
    return t;
}

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    int64_t local = tm_to_seconds(tm);
    int64_t t = local - s_tz.std_off;       /* as if standard time */
    if (s_tz.has_dst && (tm->tm_isdst > 0 || (tm->tm_isdst < 0 && in_dst(t)))) {
        t = local - s_tz.dst_off;
    }
    time_t tt = (time_t)t;
    localtime_r(&tt, tm);   /* normalize, set tm_isdst/wday/yday */
    return tt;
}

double difftime(time_t a, time_t b)
{
    return (double)(a - b);
}

void tzset(void)
{
}

char *getenv(const char *name)
{
    return (name && strcmp(name, "TZ") == 0) ? s_tz.text : NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (name && strcmp(name, "TZ") == 0) {
        bl_sdk_tz_set(value);   /* applied at once; tzset() is not needed */
    }
    return 0;
}

int unsetenv(const char *name)
{
    if (name && strcmp(name, "TZ") == 0) bl_sdk_tz_set("UTC0");
    return 0;
}

/* ---------------- strftime ---------------- */

static const char *const k_days[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const k_months[12] = {"January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December"};

typedef struct {
    char *buf;
    size_t size, n;
} out_t;

static void put(out_t *o, const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++, o->n++) {
        if (o->n + 1 < o->size) o->buf[o->n] = s[i];
    }
}

static void puts_(out_t *o, const char *s) { put(o, s, strlen(s)); }

static void putnum(out_t *o, long v, int width, char pad)
{
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), pad == '0' ? "%0*ld" : "%*ld", width, v);
    put(o, tmp, (size_t)n);
}

static void format(out_t *o, const char *fmt, const struct tm *tm);

static int week_of_year(const struct tm *tm, int first_weekday)    /* %U (Sunday) / %W (Monday) */
{
    int wday = (tm->tm_wday - first_weekday + 7) % 7;
    return (tm->tm_yday + 7 - wday) / 7;
}

static void format_one(out_t *o, char c, const struct tm *tm)
{
    int hour12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
    switch (c) {
    case 'a': put(o, k_days[tm->tm_wday % 7], 3); break;
    case 'A': puts_(o, k_days[tm->tm_wday % 7]); break;
    case 'b': case 'h': put(o, k_months[tm->tm_mon % 12], 3); break;
    case 'B': puts_(o, k_months[tm->tm_mon % 12]); break;
    case 'c': format(o, "%a %b %e %H:%M:%S %Y", tm); break;
    case 'C': putnum(o, (tm->tm_year + 1900) / 100, 2, '0'); break;
    case 'd': putnum(o, tm->tm_mday, 2, '0'); break;
    case 'D': format(o, "%m/%d/%y", tm); break;
    case 'e': putnum(o, tm->tm_mday, 2, ' '); break;
    case 'F': format(o, "%Y-%m-%d", tm); break;
    case 'H': putnum(o, tm->tm_hour, 2, '0'); break;
    case 'I': putnum(o, hour12, 2, '0'); break;
    case 'j': putnum(o, tm->tm_yday + 1, 3, '0'); break;
    case 'k': putnum(o, tm->tm_hour, 2, ' '); break;
    case 'l': putnum(o, hour12, 2, ' '); break;
    case 'm': putnum(o, tm->tm_mon + 1, 2, '0'); break;
    case 'M': putnum(o, tm->tm_min, 2, '0'); break;
    case 'n': puts_(o, "\n"); break;
    case 'p': puts_(o, tm->tm_hour < 12 ? "AM" : "PM"); break;
    case 'P': puts_(o, tm->tm_hour < 12 ? "am" : "pm"); break;
    case 'r': format(o, "%I:%M:%S %p", tm); break;
    case 'R': format(o, "%H:%M", tm); break;
    case 's': { struct tm copy = *tm; putnum(o, (long)mktime(&copy), 1, ' '); break; }
    case 'S': putnum(o, tm->tm_sec, 2, '0'); break;
    case 't': puts_(o, "\t"); break;
    case 'T': format(o, "%H:%M:%S", tm); break;
    case 'u': putnum(o, tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
    case 'U': putnum(o, week_of_year(tm, 0), 2, '0'); break;
    case 'w': putnum(o, tm->tm_wday, 1, '0'); break;
    case 'W': putnum(o, week_of_year(tm, 1), 2, '0'); break;
    case 'x': format(o, "%m/%d/%y", tm); break;
    case 'X': format(o, "%H:%M:%S", tm); break;
    case 'y': putnum(o, (tm->tm_year + 1900) % 100, 2, '0'); break;
    case 'Y': putnum(o, tm->tm_year + 1900, 1, '0'); break;
    case 'z': {
        int32_t off = tm->tm_isdst > 0 ? s_tz.dst_off : s_tz.std_off;
        char sign = off < 0 ? '-' : '+';
        if (off < 0) off = -off;
        put(o, &sign, 1);
        putnum(o, off / 3600 * 100 + off / 60 % 60, 4, '0');
        break;
    }
    case 'Z': puts_(o, tm->tm_isdst > 0 && s_tz.has_dst ? s_tz.dst_name : s_tz.std_name); break;
    case '%': puts_(o, "%"); break;
    default: { char pct[2] = {'%', c}; put(o, pct, 2); break; }
    }
}

static void format(out_t *o, const char *fmt, const struct tm *tm)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%' || !fmt[1]) {
            put(o, fmt, 1);
            continue;
        }
        fmt++;
        if (*fmt == 'E' || *fmt == 'O') fmt++;     /* locale modifiers: ignored */
        format_one(o, *fmt, tm);
    }
}

size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm)
{
    if (!buf || !size || !fmt || !tm) return 0;
    out_t o = { buf, size, 0 };
    format(&o, fmt, tm);
    if (o.n >= size) {      /* didn't fit: C says return 0 */
        buf[0] = '\0';
        return 0;
    }
    buf[o.n] = '\0';
    return o.n;
}
