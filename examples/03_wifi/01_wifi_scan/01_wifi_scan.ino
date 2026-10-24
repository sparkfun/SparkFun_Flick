// 01_wifi_scan.ino - list nearby WiFi networks every 10 seconds.
#include <WiFi.h>

void setup()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
}

void loop()
{
    Serial.println("Scanning...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("No networks found");
    }
    for (int i = 0; i < n; i++) {
        Serial.printf("%2d  %-32s  %4d dBm  ch %2d  %s\n", i + 1, WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i),
                      (int)WiFi.channel(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
    }
    WiFi.scanDelete();
    Serial.println();
    delay(10000);
}
