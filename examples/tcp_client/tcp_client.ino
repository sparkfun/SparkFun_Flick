// tcp_client.ino - talk to a server over a raw TCP connection (plain HTTP by hand).
// Set your network name and password below.
#include <WiFi.h>

const char *WIFI_SSID = "your-network";
const char *WIFI_PASSWORD = "your-password";

void setup()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.print("Connected, IP ");
    Serial.println(WiFi.localIP());

    WiFiClient client;
    if (!client.connect("example.com", 80)) {
        Serial.println("Connection failed");
        return;
    }
    client.print("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    uint32_t start = millis();
    while (client.connected() && millis() - start < 5000) {
        while (client.available()) {
            Serial.write((uint8_t)client.read());
        }
        delay(1);
    }
    client.stop();
    Serial.println("\n[connection closed]");
}

void loop()
{
    delay(1000);
}
