// Plain Arduino-ESP32 equivalent of examples/blink, for build-time comparison.
void setup()
{
    pinMode(STAT_LED, OUTPUT);
    Serial.begin(115200);
    Serial.printf("Hello from blink on Arduino-ESP32 %s\n", ESP_ARDUINO_VERSION_STR);
}

void loop()
{
    digitalWrite(STAT_LED, HIGH);
    delay(500);
    digitalWrite(STAT_LED, LOW);
    delay(500);
}
