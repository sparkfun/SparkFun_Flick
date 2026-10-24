// crash_test - used by `flick test`: crashes in setup(), so Baseline must roll
// back to the previous sketch and record the crash.
void setup()
{
    Serial.println("crash_test: writing to address 0");
    delay(100);
    volatile int *p = nullptr;
    *p = 1;
}

void loop() {}
