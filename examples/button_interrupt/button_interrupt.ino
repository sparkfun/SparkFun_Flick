// button_interrupt.ino - count presses of the BOOT button (GPIO0) with attachInterrupt.
// The handler runs on the sketch task (Baseline defers it), so Serial is safe here.

const int BUTTON = 0;
volatile int presses = 0;

void IRAM_ATTR onPress()
{
    presses++;
    Serial.printf("BOOT button pressed (%d)\n", presses);
}

void setup()
{
    pinMode(BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON), onPress, FALLING);
    Serial.println("Press the BOOT button");
}

void loop()
{
    delay(100);
}
