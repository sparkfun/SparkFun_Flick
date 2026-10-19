// uart_loopback.ino - send text out Serial1 and read it back.
// Connect a jumper between the board's TX (43) and RX (44) pins.
void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200);      // default pins: RX 44, TX 43
}

void loop()
{
    Serial1.printf("ping %lu\n", (unsigned long)millis());
    delay(100);
    if (!Serial1.available()) {
        Serial.println("nothing received - is TX (43) jumpered to RX (44)?");
    }
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }
    delay(900);
}
