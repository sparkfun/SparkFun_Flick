/*
 * sketch_ctype.c - <ctype.h> for sketches (sdk/include/compat/ctype.h): the
 * "C" locale, ASCII only, and case-insensitive string compares. Linked into
 * every sketch. Some chips have toupper/isdigit/... in ROM (rom.ld), but those
 * read newlib's locale data; these definitions take precedence over the ROM
 * ones (rom.ld only PROVIDEs).
 */
#include <stddef.h>
#include <ctype.h>

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }     /* \t \n \v \f \r */
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int isgraph(int c) { return c > 0x20 && c < 0x7F; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isascii(int c) { return c >= 0 && c < 0x80; }
int toascii(int c) { return c & 0x7F; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a) {
            return d;
        }
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a) {
            return d;
        }
    }
}
