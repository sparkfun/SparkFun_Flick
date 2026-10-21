/*
 * ESP32-C5 mask-ROM functions a sketch may call directly. These live in
 * silicon, so their addresses never change. Only functions with no global
 * state (no errno, locale, reent, or syscalls) are listed.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32c5/ld/ (esp32c5.rom.libc-suboptimal_for_misaligned_mem.ld, esp32c5.rom.libc.ld, esp32c5.rom.newlib.ld)
 */
PROVIDE(memset = 0x400004b8);
PROVIDE(memcpy = 0x400004bc);
PROVIDE(memmove = 0x400004c0);
PROVIDE(memcmp = 0x400004c4);
PROVIDE(strcpy = 0x400004c8);
PROVIDE(strncpy = 0x400004cc);
PROVIDE(strcmp = 0x400004d0);
PROVIDE(strncmp = 0x400004d4);
PROVIDE(strlen = 0x400004d8);
PROVIDE(strstr = 0x400004dc);
PROVIDE(memchr = 0x4000052c);
PROVIDE(strcat = 0x4000053c);
PROVIDE(strchr = 0x40000544);
PROVIDE(strncat = 0x40000560);
PROVIDE(strnlen = 0x40000568);
PROVIDE(strrchr = 0x4000056c);
PROVIDE(abs = 0x40000588);
PROVIDE(labs = 0x40000590);
PROVIDE(qsort = 0x40000598);
PROVIDE(toupper = 0x4000051c);
PROVIDE(tolower = 0x40000520);
PROVIDE(isalnum = 0x400004ec);
PROVIDE(isdigit = 0x40000500);
PROVIDE(strcasecmp = 0x40000534);
PROVIDE(strncasecmp = 0x4000055c);
