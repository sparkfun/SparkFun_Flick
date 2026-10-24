// wifi_http_get.ino - join WiFi and fetch a web page over HTTPS.
// Ordinary Arduino-ESP32 code: WiFi and HTTPClient run inside Baseline.
// Set your network name and password below.
#include <WiFi.h>
#include <HTTPClient.h>

#ifndef WIFI_SSID      // secrets.cfg (see secrets.cfg.example) defines these
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif
const char *URL = "https://example.com/";

void setup()
{
    Serial.begin(115200);
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 20000) {
            Serial.println("\nWiFi connection failed. Check WIFI_SSID / WIFI_PASSWORD.");
            return;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.print("\nConnected, IP address: ");
    Serial.println(WiFi.localIP());

    HTTPClient http;
    http.begin(URL);
    int code = http.GET();
    if (code > 0) {
        String payload = http.getString();
        Serial.printf("GET %s -> HTTP %d, %u bytes:\n", URL, code, payload.length());
        Serial.println(payload);
    } else {
        Serial.printf("GET failed: %s\n", HTTPClient::errorToString(code).c_str());
    }
    http.end();
}

void loop()
{
    delay(1000);
}
