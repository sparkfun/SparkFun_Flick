// blink.ino - a Flick sketch. baseline.h is included automatically.

void setup()
{
    pinMode(STAT_LED, OUTPUT);
    Serial.begin(115200);
    Serial.println("Hello from blink!");
}

void loop()
{
    digitalWrite(STAT_LED, HIGH);
    delay(500);
    digitalWrite(STAT_LED, LOW);
    delay(500);
}
