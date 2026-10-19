/*
 * baseline_string.h - Arduino's String class for sketches.
 * Heap storage comes from Baseline (malloc is forwarded). Only what a sketch
 * calls is linked into its image.
 */
#ifndef BASELINE_STRING_H
#define BASELINE_STRING_H

#ifdef __cplusplus

class String {
public:
    String(const char *s = "");
    String(const char *s, size_t len);
    String(const String &other);
    String(String &&other) noexcept;
    explicit String(char c);
    explicit String(unsigned char n, unsigned char base = 10);
    explicit String(int n, unsigned char base = 10);
    explicit String(unsigned int n, unsigned char base = 10);
    explicit String(long n, unsigned char base = 10);
    explicit String(unsigned long n, unsigned char base = 10);
    explicit String(long long n, unsigned char base = 10);
    explicit String(unsigned long long n, unsigned char base = 10);
    explicit String(float n, unsigned int decimals = 2);
    explicit String(double n, unsigned int decimals = 2);
    ~String();

    String &operator=(const String &rhs);
    String &operator=(String &&rhs) noexcept;
    String &operator=(const char *s);

    bool reserve(size_t size);
    size_t length() const { return len_; }
    bool isEmpty() const { return len_ == 0; }
    const char *c_str() const { return buf_ ? buf_ : ""; }
    explicit operator bool() const { return buf_ != 0; }

    bool concat(const String &s) { return concat(s.c_str(), s.len_); }
    bool concat(const char *s) { return s ? concat(s, strlen(s)) : false; }
    bool concat(const char *s, size_t len);
    bool concat(char c) { return concat(&c, 1); }
    template <typename T> bool concat(T value) { return concat(String(value)); }
    template <typename T> String &operator+=(const T &rhs) { concat(rhs); return *this; }

    int compareTo(const String &s) const { return strcmp(c_str(), s.c_str()); }
    bool equals(const String &s) const { return len_ == s.len_ && compareTo(s) == 0; }
    bool equals(const char *s) const { return strcmp(c_str(), s ? s : "") == 0; }
    bool equalsIgnoreCase(const String &s) const { return len_ == s.len_ && strcasecmp(c_str(), s.c_str()) == 0; }
    bool operator==(const String &s) const { return equals(s); }
    bool operator==(const char *s) const { return equals(s); }
    bool operator!=(const String &s) const { return !equals(s); }
    bool operator!=(const char *s) const { return !equals(s); }
    bool operator<(const String &s) const { return compareTo(s) < 0; }
    bool operator>(const String &s) const { return compareTo(s) > 0; }
    bool startsWith(const String &prefix, unsigned int offset = 0) const;
    bool endsWith(const String &suffix) const;

    char charAt(unsigned int i) const { return i < len_ ? buf_[i] : 0; }
    void setCharAt(unsigned int i, char c) { if (i < len_) buf_[i] = c; }
    char operator[](unsigned int i) const { return charAt(i); }
    char &operator[](unsigned int i);

    int indexOf(char c, unsigned int from = 0) const;
    int indexOf(const String &s, unsigned int from = 0) const;
    int lastIndexOf(char c) const;
    int lastIndexOf(const String &s) const;
    String substring(unsigned int from) const { return substring(from, len_); }
    String substring(unsigned int from, unsigned int to) const;

    void replace(char find, char with);
    void replace(const String &find, const String &with);
    void remove(unsigned int index) { remove(index, (unsigned int)-1); }
    void remove(unsigned int index, unsigned int count);
    void toLowerCase();
    void toUpperCase();
    void trim();

    long toInt() const { return atol(c_str()); }
    float toFloat() const { return (float)atof(c_str()); }
    double toDouble() const { return atof(c_str()); }

private:
    char *buf_ = 0;
    size_t len_ = 0;
    size_t cap_ = 0;
    void assign(const char *s, size_t len);
};

String operator+(const String &a, const String &b);
String operator+(const String &a, const char *b);
String operator+(const char *a, const String &b);
template <typename T> String operator+(const String &a, T b) { String r(a); r.concat(b); return r; }

#endif /* __cplusplus */
#endif /* BASELINE_STRING_H */
