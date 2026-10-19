// fade.ino - fade the blue STAT LED in and out with PWM (analogWrite).

int brightness = 0;
int step = 5;

void setup()
{
    Serial.println("Fading LED_BUILTIN");
}

void loop()
{
    analogWrite(LED_BUILTIN, brightness);
    brightness += step;
    if (brightness <= 0 || brightness >= 255) {
        step = -step;
    }
    delay(20);
}
