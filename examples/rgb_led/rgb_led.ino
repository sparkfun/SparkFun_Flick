// rgb_led.ino - cycle the Thing Plus WS2812 RGB LED (GPIO46) through colors.

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
