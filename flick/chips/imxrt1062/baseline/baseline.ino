// Baseline for the i.MX RT1062 (Teensy 4.0 / 4.1), built with Teensyduino by
// `flick baseline build -t teensy40`. The work is in bl_teensy_*.cpp; this file
// only gives Teensyduino its setup() and loop().
#include "bl_teensy.h"

void setup()
{
    bl_teensy_start();
}

void loop()
{
    bl_teensy_loop();
}
