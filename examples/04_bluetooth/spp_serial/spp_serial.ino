// spp_serial.ino - a Bluetooth Classic serial port (SPP), bridged to USB Serial.
// Classic ESP32 only. Pair your phone or PC with "Baseline-SPP", open it in a
// Bluetooth serial terminal app, and type: text flows both ways.
#include <BluetoothSerial.h>

BluetoothSerial SerialBT;
bool wasConnected;

void setup()
{
    Serial.begin(115200);
    if (!SerialBT.begin("Baseline-SPP")) {
        Serial.println("Bluetooth Classic is not available on this chip (classic ESP32 only).");
        return;
    }
    Serial.println("Pair with \"Baseline-SPP\" and open it in a Bluetooth serial terminal.");
}

void loop()
{
    if (SerialBT.hasClient() != wasConnected) {
        wasConnected = SerialBT.hasClient();
        Serial.println(wasConnected ? "Bluetooth device connected" : "Bluetooth device disconnected");
        if (wasConnected) {
            SerialBT.println("Hello from Baseline! Anything you type is echoed to USB Serial.");
        }
    }
    while (Serial.available()) {
        SerialBT.write(Serial.read());
    }
    while (SerialBT.available()) {
        Serial.write(SerialBT.read());
    }
    delay(20);
}
