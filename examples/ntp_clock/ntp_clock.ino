// ntp_clock.ino - get the time from the internet (NTP) and print a clock.
// Set your WiFi below, and your time zone as a POSIX TZ string.
#include <WiFi.h>
#include <time.h>

#ifndef WIFI_SSID      // secrets.cfg (see secrets.cfg.example) defines these
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif
const char *TIME_ZONE = "MST7MDT,M3.2.0,M11.1.0";   // Mountain Time (Boulder, CO)

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.nist.gov");
}

void loop()
{
    struct tm now;
    if (getLocalTime(&now)) {
        char text[64];
        strftime(text, sizeof(text), "%A %d %B %Y  %H:%M:%S %Z", &now);
        Serial.println(text);
    } else {
        Serial.println("waiting for NTP...");
    }
    delay(1000);
}
