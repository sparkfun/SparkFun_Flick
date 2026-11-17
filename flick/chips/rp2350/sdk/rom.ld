/*
 * Sketches don't call the RP2350 boot ROM: memcpy, strlen and friends come from
 * the toolchain's newlib instead (chip.json "libs": -lc, linked last, so it only
 * supplies what the SDK does not).
 */
