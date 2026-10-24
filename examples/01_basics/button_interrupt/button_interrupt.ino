// button_interrupt.ino - count presses of the BOOT button with attachInterrupt.
// The handler runs on the sketch task (Baseline defers it), so Serial is safe here.

const int BUTTON = BL_TARGET_BOOT_PIN;      // GPIO0 on the ESP32 and S3, GPIO9 on the C3 and C6
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
