// 04_analog_read.ino - read an analog pin and print the raw value and the voltage.
// The pin is A0, except on boards that name another one (BL_BOARD_ANALOG_DEMO_PIN):
// on the RedBoard Artemis ATP it is the pin printed A31 (pad 31), the last
// analog pin on its bottom header.
// On the ESP32 family the voltage uses the chip's calibration
// (analogReadMilliVolts); elsewhere it is worked out from the 0-4095 reading and
// the ADC's full scale (3.3 V, or 2.0 V on the Artemis).

#if defined(BL_BOARD_ANALOG_DEMO_PIN)
const int PIN = BL_BOARD_ANALOG_DEMO_PIN;
#define PIN_NAME BL_BOARD_ANALOG_DEMO_NAME
#else
const int PIN = A0;
#define PIN_NAME "A0"
#endif

#if defined(BL_TARGET_ADC_MV)
const long FULL_SCALE_MV = BL_TARGET_ADC_MV;
#else
const long FULL_SCALE_MV = 3300;
#endif

void setup()
{
    Serial.println("Reading pin " PIN_NAME " every half second");
}

void loop()
{
    int raw = analogRead(PIN);
#if defined(BL_PLATFORM_ESP32)
    int mv = analogReadMilliVolts(PIN);
#else
    int mv = (int)(raw * FULL_SCALE_MV / 4095);
#endif
    Serial.printf(PIN_NAME ": raw %4d  %4d mV\n", raw, mv);
    delay(500);
}
