/*
 * Baseline - the precompiled half of Baseline Sketcher.
 * Target: SparkFun ESP32-S3 Thing Plus. Build and flash with:
 *   python tools/bsk.py baseline flash -p COMx
 * (bsk sets the board options and the SDK include path; the Arduino IDE alone
 * will not build this sketch.)
 */
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
