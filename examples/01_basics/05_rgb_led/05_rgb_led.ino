// 05_rgb_led.ino - cycle the board's WS2812 RGB LED (RGB_BUILTIN) through colors.

#ifndef RGB_BUILTIN
#error "This board has no built-in RGB LED. Define RGB_BUILTIN as the pin of your WS2812."
#endif

const uint8_t colors[][3] = {
    {32, 0, 0},  {0, 32, 0},  {0, 0, 32},
    {32, 32, 0}, {0, 32, 32}, {32, 0, 32},
};

void setup()
{
    Serial.println("RGB LED demo");
}

void loop()
{
    for (const auto &c : colors) {
        rgbLedWrite(RGB_BUILTIN, c[0], c[1], c[2]);
        delay(400);
    }
}
