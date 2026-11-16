// hang_test - used by `flick test`: loop() never returns and keeps doing work,
// so the watchdog must catch it and Baseline roll back to the previous sketch.
// (An empty `while (1);` is different: Baseline treats it as a stop; see stop_test.)
volatile uint32_t spins;

void setup()
{
    Serial.println("hang_test: spinning forever");
}

void loop()
{
    while (true) {
        spins++;
    }
}
