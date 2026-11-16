// stop_test - used by `flick test`: the classic `while (1);` stops the sketch.
// Baseline must park it (INFO status "stopped"), keep uploads working, and not
// count it as a crash or roll it back.
void setup()
{
    Serial.println("stop_test: stopping in setup()");
    while (1);
}

void loop()
{
    Serial.println("stop_test: loop() must not run");
    delay(500);
}
