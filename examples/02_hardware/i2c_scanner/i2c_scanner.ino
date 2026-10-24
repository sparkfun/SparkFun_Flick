// i2c_scanner.ino - list every device on the Qwiic I2C bus (the board's SDA and SCL).

void setup()
{
    Wire.begin();
}

void loop()
{
    Serial.println("Scanning I2C bus...");
    int found = 0;
    for (int address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  device at 0x%02X\n", address);
            found++;
        }
    }
    Serial.printf("%d device(s) found\n\n", found);
    delay(5000);
}
