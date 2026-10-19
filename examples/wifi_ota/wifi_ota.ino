// wifi_ota.ino - accept sketch uploads over WiFi.
// Upload this once over USB. After that, from the same network:
//   python tools/bsk.py upload examples/wifi_ota --host baseline
// (any sketch that calls ArduinoOTA.begin() can be replaced the same way).
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

const char *WIFI_SSID = "your-network";
const char *WIFI_PASSWORD = "your-password";

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
    // ArduinoOTA.setPassword("secret");    // then upload with --password secret
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80);     // MDNS is already running for OTA
    Serial.println("Ready for WiFi uploads: bsk upload <sketch> --host baseline");
}

void loop()
{
    ArduinoOTA.handle();
    pinMode(STAT_LED, OUTPUT);
    digitalWrite(STAT_LED, (millis() / 500) % 2);
    delay(10);
}
