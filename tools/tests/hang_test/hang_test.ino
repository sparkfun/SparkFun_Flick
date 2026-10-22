// hang_test - used by `bsk test`: loop() never returns, so the task watchdog
// must catch it and Baseline roll back to the previous sketch.
void setup()
{
    Serial.println("hang_test: spinning forever");
}

void loop()
{
    while (true) {
    }
}
