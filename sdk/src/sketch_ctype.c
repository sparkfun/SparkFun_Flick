/*
 * sketch_ctype.c - toupper/tolower/isalnum/isdigit and case-insensitive string
 * compares, for targets whose ROM versions can't be called directly (they need
 * newlib's ROM locale data). Linked only for targets that list it in
 * target.json "sdk_sources"; others take these from their ROM (rom.ld).
 */
#include <stddef.h>

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isdigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

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
