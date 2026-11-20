// Baseline for the Apollo3 (SparkFun Artemis), built by
// `flick baseline build -t redboard_artemis_atp` (or another Artemis target).
// The work is in bl_apollo3_*.cpp; this file only gives the core its setup() and loop().
#include "bl_apollo3.h"

void setup()
{
    bl_apollo3_start();
}

void loop()
{
    bl_apollo3_loop();
}
