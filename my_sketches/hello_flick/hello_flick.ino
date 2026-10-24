// hello_flick.ino - your first sketch: blink the board's LED and say hello.
//
// This one is yours to change. Edit the message or the delays, then press
// Ctrl+Shift+B: the new sketch is running on the board about a second later.
// (The untouched original is examples/01_basics/01_hello_flick.)

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    Serial.println("Hello flick!");

    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
