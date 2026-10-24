// ota_pull_sketch.ino - the board checks a web server for a newer version of
// this sketch, downloads it and restarts into it. No PC connection needed.
//
// The server holds two files side by side:
//   sketch-version.txt     the newest version number, e.g. 2
//   ota_pull_sketch.flick    that version of the sketch (flick build writes it to
//                          build/<target>/ota_pull_sketch.flick)
// Any web server works: GitHub releases, your own, or your PC.
//
// Try it with your PC as the server, on the same network as the board:
//   1. Set UPDATE_URL below to http://<your PC's IP>:8000 and upload this
//      sketch over USB. It prints "running version 1".
//   2. Change SKETCH_VERSION to 2 and build: python flick.py build examples/07_updates/ota_pull_sketch
//   3. Put the .flick in a folder with sketch-version.txt containing 2, and serve
//      that folder: python -m http.server 8000
// Within a minute the board installs version 2 and prints "running version 2".
// If a new version crashed on its first run, Baseline would go back to the old one.
//
// WiFi: WIFI_SSID / WIFI_PASSWORD from secrets.cfg (see secrets.cfg.example).
#include <WiFi.h>
#include <HTTPClient.h>

#define SKETCH_VERSION 1

#ifndef UPDATE_URL
#define UPDATE_URL "http://192.168.1.100:8000"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif

const unsigned long CHECK_EVERY_MS = 60 * 1000;
unsigned long lastCheck;

// The newest version on the server, or -1 if it could not be read.
int latestVersion()
{
    HTTPClient http;
    http.begin(UPDATE_URL "/sketch-version.txt");
    int code = http.GET();
    int version = -1;
    if (code == 200) {
        version = http.getString().toInt();
    } else {
        Serial.printf("version check failed: HTTP %d\n", code);
    }
    http.end();
    return version;
}

void checkForUpdate()
{
    int latest = latestVersion();
    if (latest <= SKETCH_VERSION) {
        Serial.printf("up to date (version %d)\n", SKETCH_VERSION);
        return;
    }
    Serial.printf("installing version %d...\n", latest);
    int err = BaselineUpdate.sketchFromUrl(UPDATE_URL "/ota_pull_sketch.flick");   // restarts when it works
    Serial.printf("update failed (%d); trying again later\n", err);
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.printf("running version %d\n", SKETCH_VERSION);
    checkForUpdate();
    lastCheck = millis();
}

void loop()
{
    if (millis() - lastCheck > CHECK_EVERY_MS) {
        lastCheck = millis();
        checkForUpdate();
    }
    delay(100);
}
