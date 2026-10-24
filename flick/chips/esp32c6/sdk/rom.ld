/*
 * ESP32-C6 mask-ROM functions a sketch may call directly. These live in
 * silicon, so their addresses never change. Only functions with no global
 * state (no errno, locale, reent, or syscalls) are listed.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32c6/ld/ (esp32c6.rom.libc-suboptimal_for_misaligned_mem.ld, esp32c6.rom.libc.ld, esp32c6.rom.newlib.ld)
 */
PROVIDE(memset = 0x400004a8);
PROVIDE(memcpy = 0x400004ac);
PROVIDE(memmove = 0x400004b0);
PROVIDE(memcmp = 0x400004b4);
PROVIDE(strcpy = 0x400004b8);
PROVIDE(strncpy = 0x400004bc);
PROVIDE(strcmp = 0x400004c0);
PROVIDE(strncmp = 0x400004c4);
PROVIDE(strlen = 0x400004c8);
PROVIDE(strstr = 0x400004cc);
PROVIDE(memchr = 0x4000051c);
PROVIDE(strcat = 0x4000052c);
PROVIDE(strchr = 0x40000534);
PROVIDE(strncat = 0x40000550);
PROVIDE(strnlen = 0x40000558);
PROVIDE(strrchr = 0x4000055c);
PROVIDE(abs = 0x40000578);
PROVIDE(labs = 0x40000580);
PROVIDE(qsort = 0x40000588);
PROVIDE(toupper = 0x4000050c);
PROVIDE(tolower = 0x40000510);
PROVIDE(isalnum = 0x400004dc);
PROVIDE(isdigit = 0x400004f0);
PROVIDE(strcasecmp = 0x40000524);
PROVIDE(strncasecmp = 0x4000054c);
