/*
 * ESP32 mask-ROM functions a sketch may call directly. These live in silicon,
 * so their addresses never change. Only the pure string/memory functions are
 * listed: the ROM's ctype and strcasecmp go through newlib's ROM locale data,
 * so sdk/src/sketch_libc.c supplies those instead.
 * Source: ESP-IDF 5.5 components/esp_rom/esp32/ld/esp32.rom.libc-funcs.ld
 */
PROVIDE(memchr = 0x4000c244);
PROVIDE(memcmp = 0x4000c260);
PROVIDE(memcpy = 0x4000c2c8);
PROVIDE(memmove = 0x4000c3c0);
PROVIDE(memset = 0x4000c44c);
PROVIDE(strcat = 0x4000c518);
PROVIDE(strchr = 0x4000c53c);
PROVIDE(strcmp = 0x40001274);
PROVIDE(strcpy = 0x400013ac);
PROVIDE(strlen = 0x400014c0);
PROVIDE(strncat = 0x4000c5c4);
PROVIDE(strncmp = 0x4000c5f4);
PROVIDE(strncpy = 0x400015d4);
PROVIDE(strnlen = 0x4000c628);
PROVIDE(strrchr = 0x40001708);
PROVIDE(strstr = 0x4000c674);
PROVIDE(abs = 0x40056340);
PROVIDE(labs = 0x40056370);
PROVIDE(qsort = 0x40056424);
