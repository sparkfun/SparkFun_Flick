// touch_button.ino - use a bare wire or pad as a touch button.
// Connect a jumper wire to the touch pin below and touch its end.
// Classic ESP32: readings DROP when touched. ESP32-S3: readings RISE.
#ifdef BL_TOUCH_T0
const int TOUCH_PIN = T6;       // Thing Plus C: GPIO14
#else
const int TOUCH_PIN = T5;       // S3 Thing Plus: GPIO5
#endif

uint32_t untouched;
volatile int touches;

void onTouch()
{
    touches++;
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    untouched = touchRead(TOUCH_PIN);
    Serial.printf("Untouched reading: %u\n", (unsigned)untouched);
#ifdef BL_TOUCH_T0
    touchAttachInterrupt(TOUCH_PIN, onTouch, untouched * 2 / 3);    // below 2/3 = touched
#else
    touchAttachInterrupt(TOUCH_PIN, onTouch, untouched * 3 / 2);    // above 1.5x = touched
#endif
}

void loop()
{
    Serial.printf("reading %u, touches so far %d\n", (unsigned)touchRead(TOUCH_PIN), touches);
    delay(500);
}
