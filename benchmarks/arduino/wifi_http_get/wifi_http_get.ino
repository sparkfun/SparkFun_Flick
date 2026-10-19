// Plain Arduino-ESP32 equivalent of examples/wifi_http_get, for build-time comparison.
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

const char *WIFI_SSID = "your-network";
const char *WIFI_PASSWORD = "your-password";
const char *URL = "https://example.com/";

void setup()
{
    Serial.begin(115200);
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 20000) {
            Serial.println("\nWiFi connection failed.");
            return;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.printf("\nConnected: IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

    NetworkClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, URL);
    int status = http.GET();
    String body = http.getString();
    Serial.printf("GET %s -> HTTP %d, %u bytes:\n", URL, status, body.length());
    Serial.println(body);
    http.end();
}

void loop()
{
    delay(1000);
}
