// analog_read.ino - read pin A0 (GPIO10) and print the raw value and voltage.

void setup()
{
    Serial.println("Reading A0 every half second");
}

void loop()
{
    int raw = analogRead(A0);
    int mv = analogReadMilliVolts(A0);
    Serial.printf("A0: raw %4d  %4d mV\n", raw, mv);
    delay(500);
}
