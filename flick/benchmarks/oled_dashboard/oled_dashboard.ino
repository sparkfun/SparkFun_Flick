// oled_dashboard.ino - a live dashboard on SparkFun's 1.3" Qwiic OLED.
// The board comparison benchmark (docs/BOARD_COMPARISON.md): this one file
// builds unchanged with the Arduino IDE and with Flick, on every board.
// Without an OLED it still runs, and says so.
// Needs the "SparkFun Qwiic OLED Arduino Library" (Library Manager).
#include <Wire.h>
#include <math.h>
#include <SparkFun_Qwiic_OLED.h>

Qwiic1in3OLED oled;             // 128 x 64, I2C address 0x3D
bool haveOled = false;
uint32_t frames = 0;
uint32_t lastReport = 0;
float phase = 0;

void scanBus()
{
    Serial.print("I2C devices:");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf(" 0x%02X", addr);
            found++;
        }
    }
    Serial.println(found ? "" : " none");
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);
    scanBus();
    haveOled = oled.begin();
    Serial.println(haveOled ? "OLED found" : "OLED not found: drawing to nowhere");
}

void loop()
{
    char line[32];
    if (haveOled) {
        oled.erase();
        oled.text(0, 0, "Flick dashboard");
        snprintf(line, sizeof(line), "up %lu s", (unsigned long)(millis() / 1000));
        oled.text(0, 12, line);
        // A moving sine wave across the lower part of the screen.
        int lastY = -1;
        for (int x = 0; x < 128; x++) {
            int y = 46 + (int)(14.0f * sinf(phase + x * 0.098f));
            if (lastY >= 0) {
                oled.line(x - 1, lastY, x, y);
            }
            lastY = y;
        }
        oled.display();
    } else {
        delay(20);              // no display: about the same pace
    }
    phase += 0.15f;
    frames++;
    if (millis() - lastReport >= 1000) {
        Serial.printf("%lu frames/s\n", (unsigned long)frames);
        frames = 0;
        lastReport = millis();
    }
}
