// led_check.ino - a slow, endless LED check for bring-up.
// Holds each color for one second: red, green, blue, white, off, and repeats.
// The console names each step, so you can compare it with what you see.
// Boards with a separate plain status LED blink it in the "off" step.

struct Step {
    const char *name;
    uint8_t r, g, b;
};

const Step steps[] = {
    {"red", 32, 0, 0},
    {"green", 0, 32, 0},
    {"blue", 0, 0, 32},
    {"white", 32, 32, 32},
    {"off", 0, 0, 0},
};

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    for (const Step &s : steps) {
        Serial.printf("LED: %s\n", s.name);
#ifdef RGB_BUILTIN
        rgbLedWrite(RGB_BUILTIN, s.r, s.g, s.b);
        const bool plain_led = LED_BUILTIN != RGB_BUILTIN;
#else
        const bool plain_led = true;
#endif
        if (plain_led && s.r == 0 && s.g == 0 && s.b == 0) {
            Serial.println("LED: status LED on");
            digitalWrite(LED_BUILTIN, HIGH);
            delay(500);
            digitalWrite(LED_BUILTIN, LOW);
            delay(500);
        } else {
            delay(1000);
        }
    }
}
