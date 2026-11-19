/*
 * The Apollo3 boot ROM has no C library: memcpy, strlen and friends come from
 * the toolchain's newlib instead (chip.json "libs": -lc, linked last, so it only
 * supplies what the SDK does not).
 */
