/*
 * ESP32-S3 mask-ROM functions a sketch may call directly. These live in
 * silicon, so their addresses never change. Only functions with no global
 * state (no errno, locale, reent, or syscalls) are listed.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld / .newlib.ld
 */
PROVIDE(memset = 0x400011e8);
PROVIDE(memcpy = 0x400011f4);
PROVIDE(memmove = 0x40001200);
PROVIDE(memcmp = 0x4000120c);
PROVIDE(strcpy = 0x40001218);
PROVIDE(strncpy = 0x40001224);
PROVIDE(strcmp = 0x40001230);
PROVIDE(strncmp = 0x4000123c);
PROVIDE(strlen = 0x40001248);
PROVIDE(strstr = 0x40001254);
PROVIDE(memchr = 0x40001344);
PROVIDE(strcat = 0x40001374);
PROVIDE(strchr = 0x4000138c);
PROVIDE(strncat = 0x400013e0);
PROVIDE(strnlen = 0x400013f8);
PROVIDE(strrchr = 0x40001404);
PROVIDE(abs = 0x40001458);
PROVIDE(labs = 0x40001470);
PROVIDE(qsort = 0x40001488);
PROVIDE(toupper = 0x40001314);
PROVIDE(tolower = 0x40001320);
PROVIDE(isalnum = 0x40001284);
PROVIDE(isdigit = 0x400012c0);
PROVIDE(strcasecmp = 0x4000135c);
PROVIDE(strncasecmp = 0x400013d4);
