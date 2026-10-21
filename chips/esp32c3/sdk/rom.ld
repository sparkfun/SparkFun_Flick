/*
 * ESP32-C3 mask-ROM functions a sketch may call directly. These live in
 * silicon, so their addresses never change. Only functions with no global
 * state (no errno, locale, reent, or syscalls) are listed.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32c3/ld/ (esp32c3.rom.libc-suboptimal_for_misaligned_mem.ld, esp32c3.rom.libc.ld, esp32c3.rom.newlib.ld)
 */
PROVIDE(memset = 0x40000354);
PROVIDE(memcpy = 0x40000358);
PROVIDE(memmove = 0x4000035c);
PROVIDE(memcmp = 0x40000360);
PROVIDE(strcpy = 0x40000364);
PROVIDE(strncpy = 0x40000368);
PROVIDE(strcmp = 0x4000036c);
PROVIDE(strncmp = 0x40000370);
PROVIDE(strlen = 0x40000374);
PROVIDE(strstr = 0x40000378);
PROVIDE(memchr = 0x400003c8);
PROVIDE(strcat = 0x400003d8);
PROVIDE(strchr = 0x400003e0);
PROVIDE(strncat = 0x400003fc);
PROVIDE(strnlen = 0x40000404);
PROVIDE(strrchr = 0x40000408);
PROVIDE(abs = 0x40000424);
PROVIDE(labs = 0x4000042c);
PROVIDE(qsort = 0x40000434);
PROVIDE(toupper = 0x400003b8);
PROVIDE(tolower = 0x400003bc);
PROVIDE(isalnum = 0x40000388);
PROVIDE(isdigit = 0x4000039c);
PROVIDE(strcasecmp = 0x400003d0);
PROVIDE(strncasecmp = 0x400003f8);
