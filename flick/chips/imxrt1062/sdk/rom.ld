/*
 * The i.MX RT1062 boot ROM has no C library functions a sketch could call.
 * memcpy, strlen and friends come from the toolchain's newlib instead (chip.json
 * "libs": -lc, linked last, so it only supplies what the SDK does not).
 *
 * Baseline's RAM vector table, at a fixed address (BL_TEENSY_VECTORS in
 * bl_target.h; Baseline's linker script checks it), so imxrt.h's
 * attachInterruptVector() works in sketches.
 */
PROVIDE(_VectorsRam = 0x20000000);
