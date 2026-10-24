// Plain Arduino-ESP32 equivalent of benchmarks/flick/hello_world, for build-time comparison.
int count = 0;

void setup()
{
    Serial.begin(115200);
    Serial.println("Hello, world!");
    Serial.printf("Running on Arduino-ESP32 %s with %lu bytes of heap free\n",
                  ESP_ARDUINO_VERSION_STR, (unsigned long)ESP.getFreeHeap());
}

void loop()
{
    Serial.printf("Hello #%d at %lu ms\n", count++, (unsigned long)millis());
    delay(1000);
}
