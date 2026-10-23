/*
 * ctype.h - character classes for sketches (the "C" locale: ASCII).
 *
 * Replaces newlib's <ctype.h>, whose macros read locale tables that Baseline
 * does not export to sketches. These are plain functions (sdk/src/sketch_ctype.c),
 * so <cctype> and std::isdigit work too. Arduino's isDigit(), isAlpha(), ...
 * are in baseline_arduino.h.
 */
#ifndef _CTYPE_H_
#define _CTYPE_H_

/* newlib's class bits: libstdc++'s <bits/ctype_base.h> (via <string>,
 * <locale>, ...) defines std::ctype_base masks with them. */
#define _U  01
#define _L  02
#define _N  04
#define _S  010
#define _P  020
#define _C  040
#define _X  0100
#define _B  0200

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int isascii(int c);
int toascii(int c);
int tolower(int c);
int toupper(int c);

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H_ */
