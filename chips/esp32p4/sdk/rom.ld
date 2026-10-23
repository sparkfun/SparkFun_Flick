/*
 * ESP32-P4 mask-ROM functions a sketch may call directly. These live in
 * silicon, so their addresses never change. Only functions with no global
 * state (no errno, locale, reent, or syscalls) are listed.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32p4/ld/ (esp32p4.rom.libc-suboptimal_for_misaligned_mem.ld, esp32p4.rom.eco5.libc.ld,
 * esp32p4.rom.eco5.newlib.ld: rev 3.x silicon). The addresses are the same in the pre-ECO5 files.
 */
PROVIDE(memset = 0x4fc00268);
PROVIDE(memcpy = 0x4fc0026c);
PROVIDE(memmove = 0x4fc00270);
PROVIDE(memcmp = 0x4fc00274);
PROVIDE(strcpy = 0x4fc00278);
PROVIDE(strncpy = 0x4fc0027c);
PROVIDE(strcmp = 0x4fc00280);
PROVIDE(strncmp = 0x4fc00284);
PROVIDE(strlen = 0x4fc00288);
PROVIDE(strstr = 0x4fc0028c);
PROVIDE(memchr = 0x4fc002dc);
PROVIDE(strcat = 0x4fc002ec);
PROVIDE(strchr = 0x4fc002f4);
PROVIDE(strncat = 0x4fc00310);
PROVIDE(strnlen = 0x4fc00318);
PROVIDE(strrchr = 0x4fc0031c);
PROVIDE(abs = 0x4fc00338);
PROVIDE(labs = 0x4fc00340);
PROVIDE(qsort = 0x4fc00348);
PROVIDE(toupper = 0x4fc002cc);
PROVIDE(tolower = 0x4fc002d0);
PROVIDE(isalnum = 0x4fc0029c);
PROVIDE(isdigit = 0x4fc002b0);
PROVIDE(strcasecmp = 0x4fc002e4);
PROVIDE(strncasecmp = 0x4fc0030c);
