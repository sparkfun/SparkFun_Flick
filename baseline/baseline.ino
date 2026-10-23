/*
 * Baseline - the precompiled half of Flick.
 * This folder is the source shared by every ESP32-family target; each target
 * adds its own files from targets/<target>/baseline/. Build and flash with:
 *   python tools/flick.py baseline flash --target esp32s3     (or esp32, ...)
 * (flick assembles the target's copy of this folder and sets the board options
 * and include paths; the Arduino IDE alone will not build this sketch.)
 */
#include <Arduino.h>     // explicit: the ESP-IDF build compiles this as plain C++
#include "src/bl_main.h"

void setup()
{
    bl_main();
}

void loop()
{
    // Baseline runs in its own tasks; the Arduino loop task is not needed.
    vTaskDelete(NULL);
}
