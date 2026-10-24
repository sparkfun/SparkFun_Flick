// 01_ble_serial.ino - a BLE serial port (Nordic UART Service) bridged to USB.
// Connect with nRF Connect, Bluefruit Connect, or "Serial Bluetooth Terminal".
// Text typed in the USB monitor goes to the phone, and the other way round.

bool wasConnected = false;

void setup()
{
    BLESerial.begin("Baseline-UART");
    Serial.println("Advertising as 'Baseline-UART'");
}

void loop()
{
    bool connected = BLESerial.connected();
    if (connected != wasConnected) {
        wasConnected = connected;
        Serial.println(connected ? "BLE connected" : "BLE disconnected");
        if (connected) {
            BLESerial.println("Hello from Flick!");
        }
    }

    while (Serial.available()) {
        BLESerial.write((uint8_t)Serial.read());
    }
    while (BLESerial.available()) {
        Serial.write((uint8_t)BLESerial.read());
    }
    delay(10);
}
