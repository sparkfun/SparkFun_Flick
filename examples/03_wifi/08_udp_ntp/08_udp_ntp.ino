// 08_udp_ntp.ino - get the time from an NTP server with raw UDP packets.
// Set your network name and password below.
#include <WiFi.h>
#include <WiFiUdp.h>

#ifndef WIFI_SSID      // set them once with: python flick.py wifi "network" "password"
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif
const char *NTP_SERVER = "pool.ntp.org";

WiFiUDP udp;
uint8_t packet[48];

void setup()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.print("\nConnected, IP ");
    Serial.println(WiFi.localIP());
    udp.begin(2390);
}

void loop()
{
    memset(packet, 0, sizeof(packet));
    packet[0] = 0b11100011;     // LI = unsynchronized, version 4, client mode
    udp.beginPacket(NTP_SERVER, 123);
    udp.write(packet, sizeof(packet));
    udp.endPacket();

    uint32_t start = millis();
    while (millis() - start < 2000) {
        if (udp.parsePacket() >= 48) {
            udp.read(packet, sizeof(packet));
            uint32_t seconds1900 = (uint32_t)packet[40] << 24 | (uint32_t)packet[41] << 16 | packet[42] << 8 | packet[43];
            uint32_t unix = seconds1900 - 2208988800UL;
            Serial.printf("UTC time: %02lu:%02lu:%02lu (unix %lu) from ", (unix % 86400) / 3600, (unix % 3600) / 60,
                          unix % 60, (unsigned long)unix);
            Serial.println(udp.remoteIP());
            break;
        }
        delay(10);
    }
    delay(10000);
}
