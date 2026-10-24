/*
 * baseline_pgmspace.h - AVR-style PROGMEM names, as Arduino-ESP32 defines them.
 *
 * On the ESP32 family constant data already lives in flash and is read like any
 * other memory, so PROGMEM is empty and the pgm_read_* accessors are plain
 * loads. Libraries written for AVR (ArduinoJson, display fonts, ...) use these.
 * Included by baseline.h; <pgmspace.h> and <avr/pgmspace.h> include it too.
 */
#ifndef BASELINE_PGMSPACE_H
#define BASELINE_PGMSPACE_H

#include <stdint.h>
#include <string.h>

#define PROGMEM
#define PGM_P                 const char *
#define PGM_VOID_P            const void *
#define PSTR(s)               (s)

#define pgm_read_byte(addr)   (*(const unsigned char *)(addr))
#define pgm_read_word(addr)   ({ typeof(addr) _addr = (addr); *(const unsigned short *)(_addr); })
#define pgm_read_dword(addr)  ({ typeof(addr) _addr = (addr); *(const unsigned long *)(_addr); })
#define pgm_read_float(addr)  ({ typeof(addr) _addr = (addr); *(const float *)(_addr); })
#define pgm_read_ptr(addr)    ({ typeof(addr) _addr = (addr); *(void * const *)(_addr); })

#define pgm_read_byte_near(addr)  pgm_read_byte(addr)
#define pgm_read_word_near(addr)  pgm_read_word(addr)
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#define pgm_read_float_near(addr) pgm_read_float(addr)
#define pgm_read_ptr_near(addr)   pgm_read_ptr(addr)
#define pgm_read_byte_far(addr)   pgm_read_byte(addr)
#define pgm_read_word_far(addr)   pgm_read_word(addr)
#define pgm_read_dword_far(addr)  pgm_read_dword(addr)
#define pgm_read_float_far(addr)  pgm_read_float(addr)
#define pgm_read_ptr_far(addr)    pgm_read_ptr(addr)

#define memcmp_P      memcmp
#define memccpy_P     memccpy
#define memmem_P      memmem
#define memcpy_P      memcpy
#define strcpy_P      strcpy
#define strncpy_P     strncpy
#define strcat_P      strcat
#define strncat_P     strncat
#define strcmp_P      strcmp
#define strncmp_P     strncmp
#define strcasecmp_P  strcasecmp
#define strncasecmp_P strncasecmp
#define strlen_P      strlen
#define strnlen_P     strnlen
#define strstr_P      strstr
#define printf_P      printf
#define sprintf_P     sprintf
#define snprintf_P    snprintf
#define vsnprintf_P   vsnprintf

#endif /* BASELINE_PGMSPACE_H */
