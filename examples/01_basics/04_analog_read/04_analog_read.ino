// 04_analog_read.ino - read pin A0 and print the raw value and the voltage.
// On the ESP32 family the voltage uses the chip's calibration
// (analogReadMilliVolts); elsewhere it is worked out from the 0-4095 reading.

void setup()
{
    Serial.println("Reading A0 every half second");
}

void loop()
{
    int raw = analogRead(A0);
#if defined(BL_PLATFORM_ESP32)
    int mv = analogReadMilliVolts(A0);
#else
    int mv = (int)(raw * 3300L / 4095);     // 3.3 V full scale
#endif
    Serial.printf("A0: raw %4d  %4d mV\n", raw, mv);
    delay(500);
}
