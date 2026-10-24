// 01_wifi_ota.ino - update this board over WiFi from your PC: the sketch, and Baseline itself.
//
// Upload this once over USB. After that, from a PC on the same network:
//   python flick.py upload my_sketch --host baseline          a new sketch
//   python flick.py baseline update --host baseline           a new Baseline (built for this board)
//   python flick.py baseline update --host baseline --file baseline-<target>.bsu   a downloaded one
// Any sketch that calls ArduinoOTA.begin() can be updated the same way.
//
// Baseline updates: boards with 8 or 16 MB of flash keep running this sketch
// while the new Baseline arrives, then restart into it (the old Baseline comes
// back if the new one fails to start). On 4 MB boards (Pro Micro C3, Qwiic
// Pocket C6, Thing Plus S3) Baseline first restarts without the sketch, and
// the update erases the sketch and LittleFS: upload a sketch again afterwards.
//
// WiFi: WIFI_SSID / WIFI_PASSWORD from secrets.cfg (see secrets.cfg.example).
#include <WiFi.h>
#include <ArduinoOTA.h>

#ifndef WIFI_SSID
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\nWiFi connected: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.setHostname("baseline");     // reachable as baseline.local
    // ArduinoOTA.setPassword("secret");    // then add --password secret
    ArduinoOTA.begin();
    Serial.printf("Ready for WiFi updates (Baseline %s, update mode %s)\n", BaselineUpdate.baselineVersion(),
                  BaselineUpdate.modeName());
}

void loop()
{
    ArduinoOTA.handle();
    pinMode(STAT_LED, OUTPUT);
    digitalWrite(STAT_LED, (millis() / 500) % 2);
    delay(10);
}
