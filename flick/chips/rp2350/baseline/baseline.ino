// Baseline for the RP2350 (arduino-pico), built by `flick baseline build -t pro_micro_rp2350`
// (or another RP2350 target). The work is in bl_rp2_*.cpp; this file only gives
// arduino-pico its setup() and loop().
#include "bl_rp2.h"

void setup()
{
    bl_rp2_start();
}

void loop()
{
    bl_rp2_loop();
}
