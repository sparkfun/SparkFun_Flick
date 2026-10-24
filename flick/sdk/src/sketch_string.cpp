/* sketch_string.cpp - Arduino String implementation (see baseline_string.h). */
#include "baseline.h"

static const char *number_text(char *buf, size_t size, unsigned long long n, bool neg, unsigned base)
{
    char *p = buf + size;
    *--p = '\0';
    if (base < 2 || base > 36) {
        base = 10;
    }
    do {
        unsigned d = (unsigned)(n % base);
        *--p = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        n /= base;
    } while (n);
    if (neg) {
        *--p = '-';
    }
    return p;
}

static String from_signed(long long n, unsigned char base)
{
    char buf[72];
    if (base == 10 && n < 0) {
        return String(number_text(buf, sizeof(buf), 0ULL - (unsigned long long)n, true, base));
    }
    return String(number_text(buf, sizeof(buf), (unsigned long long)n, false, base));
}

String::String(const char *s) { assign(s ? s : "", s ? strlen(s) : 0); }
String::String(const char *s, size_t len) { assign(s, len); }
String::String(const String &other) { assign(other.c_str(), other.len_); }
String::String(String &&other) noexcept : buf_(other.buf_), len_(other.len_), cap_(other.cap_)
{
    other.buf_ = 0;
    other.len_ = other.cap_ = 0;
}
String::String(char c) { assign(&c, 1); }
String::String(unsigned char n, unsigned char base) : String((unsigned long long)n, base) {}
String::String(int n, unsigned char base) : String(from_signed(n, base)) {}
String::String(unsigned int n, unsigned char base) : String((unsigned long long)n, base) {}
String::String(long n, unsigned char base) : String(from_signed(n, base)) {}
String::String(unsigned long n, unsigned char base) : String((unsigned long long)n, base) {}
String::String(long long n, unsigned char base) : String(from_signed(n, base)) {}
String::String(unsigned long long n, unsigned char base)
{
    char buf[72];
    const char *t = number_text(buf, sizeof(buf), n, false, base);
    assign(t, strlen(t));
}
String::String(float n, unsigned int decimals) : String((double)n, decimals) {}
String::String(double n, unsigned int decimals)
{
    char buf[48];
    int len = snprintf(buf, sizeof(buf), "%.*f", (int)decimals, n);
    assign(buf, len < (int)sizeof(buf) ? len : sizeof(buf) - 1);
}
String::~String() { free(buf_); }

String &String::operator=(const String &rhs)
{
    if (this != &rhs) {
        assign(rhs.c_str(), rhs.len_);
    }
    return *this;
}

String &String::operator=(String &&rhs) noexcept
{
    if (this != &rhs) {
        free(buf_);
        buf_ = rhs.buf_;
        len_ = rhs.len_;
        cap_ = rhs.cap_;
        rhs.buf_ = 0;
        rhs.len_ = rhs.cap_ = 0;
    }
    return *this;
}

String &String::operator=(const char *s)
{
    assign(s ? s : "", s ? strlen(s) : 0);
    return *this;
}

bool String::reserve(size_t size)
{
    if (buf_ && cap_ >= size) {
        return true;
    }
    char *p = (char *)realloc(buf_, size + 1);
    if (!p) {
        return false;
    }
    if (!buf_) {
        p[0] = '\0';
    }
    buf_ = p;
    cap_ = size;
    return true;
}

void String::assign(const char *s, size_t len)
{
    if (!reserve(len)) {
        free(buf_);
        buf_ = 0;
        len_ = cap_ = 0;
        return;
    }
    memmove(buf_, s, len);
    buf_[len] = '\0';
    len_ = len;
}

bool String::concat(const char *s, size_t len)
{
    if (!len) {
        return true;
    }
    if (!reserve(len_ + len)) {
        return false;
    }
    memmove(buf_ + len_, s, len);
    len_ += len;
    buf_[len_] = '\0';
    return true;
}

char &String::operator[](unsigned int i)
{
    static char dummy;
    if (i >= len_) {
        dummy = 0;
        return dummy;
    }
    return buf_[i];
}

bool String::startsWith(const String &prefix, unsigned int offset) const
{
    return offset + prefix.len_ <= len_ && strncmp(c_str() + offset, prefix.c_str(), prefix.len_) == 0;
}

bool String::endsWith(const String &suffix) const
{
    return suffix.len_ <= len_ && strcmp(c_str() + len_ - suffix.len_, suffix.c_str()) == 0;
}

int String::indexOf(char c, unsigned int from) const
{
    if (from >= len_) {
        return -1;
    }
    const char *p = strchr(c_str() + from, c);
    return p ? (int)(p - c_str()) : -1;
}

int String::indexOf(const String &s, unsigned int from) const
{
    if (from > len_) {
        return -1;
    }
    const char *p = strstr(c_str() + from, s.c_str());
    return p ? (int)(p - c_str()) : -1;
}

int String::lastIndexOf(char c) const
{
    const char *p = strrchr(c_str(), c);
    return p ? (int)(p - c_str()) : -1;
}

int String::lastIndexOf(const String &s) const
{
    int found = -1;
    for (int i = indexOf(s); i >= 0; i = indexOf(s, i + 1)) {
        found = i;
    }
    return found;
}

String String::substring(unsigned int from, unsigned int to) const
{
    if (from > to) {
        unsigned int t = from;
        from = to;
        to = t;
    }
    if (from >= len_) {
        return String();
    }
    if (to > len_) {
        to = len_;
    }
    return String(c_str() + from, to - from);
}

void String::replace(char find, char with)
{
    for (size_t i = 0; i < len_; i++) {
        if (buf_[i] == find) {
            buf_[i] = with;
        }
    }
}

void String::replace(const String &find, const String &with)
{
    if (!find.len_ || !len_) {
        return;
    }
    String out;
    unsigned int pos = 0;
    for (int i = indexOf(find); i >= 0; i = indexOf(find, pos)) {
        out.concat(c_str() + pos, i - pos);
        out.concat(with);
        pos = i + find.len_;
    }
    out.concat(c_str() + pos, len_ - pos);
    *this = static_cast<String &&>(out);
}

void String::remove(unsigned int index, unsigned int count)
{
    if (index >= len_) {
        return;
    }
    if (count > len_ - index) {
        count = len_ - index;
    }
    memmove(buf_ + index, buf_ + index + count, len_ - index - count + 1);
    len_ -= count;
}

void String::toLowerCase()
{
    for (size_t i = 0; i < len_; i++) {
        if (buf_[i] >= 'A' && buf_[i] <= 'Z') buf_[i] += 'a' - 'A';
    }
}

void String::toUpperCase()
{
    for (size_t i = 0; i < len_; i++) {
        if (buf_[i] >= 'a' && buf_[i] <= 'z') buf_[i] -= 'a' - 'A';
    }
}

void String::trim()
{
    if (!len_) {
        return;
    }
    size_t start = 0, end = len_;
    while (start < end && (buf_[start] == ' ' || (buf_[start] >= '\t' && buf_[start] <= '\r'))) start++;
    while (end > start && (buf_[end - 1] == ' ' || (buf_[end - 1] >= '\t' && buf_[end - 1] <= '\r'))) end--;
    memmove(buf_, buf_ + start, end - start);
    len_ = end - start;
    buf_[len_] = '\0';
}

String operator+(const String &a, const String &b) { String r(a); r.concat(b); return r; }
String operator+(const String &a, const char *b) { String r(a); r.concat(b); return r; }
String operator+(const char *a, const String &b) { String r(a); r.concat(b); return r; }
