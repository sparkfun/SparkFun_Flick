// 03_uart_loopback.ino - send text out Serial1 and read it back.
// Connect a jumper between the board's Serial1 TX and RX pins (the sketch prints them).
void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200);      // the board's default Serial1 pins
    Serial.printf("Serial1: TX is GPIO %d, RX is GPIO %d\n", BL_SERIAL1_TX, BL_SERIAL1_RX);
}

void loop()
{
    Serial1.printf("ping %lu\n", (unsigned long)millis());
    delay(100);
    if (!Serial1.available()) {
        Serial.printf("nothing received - is TX (%d) jumpered to RX (%d)?\n", BL_SERIAL1_TX, BL_SERIAL1_RX);
    }
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }
    delay(900);
}
