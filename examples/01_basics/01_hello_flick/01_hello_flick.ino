// 01_hello_flick.ino - blink the board's LED and say hello.
//
// Change something (the message, or the delay) and upload again:
// the new sketch is running about a second later.

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
