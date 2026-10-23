// ota_pull_baseline.ino - the board checks a web server for a newer Baseline
// (the precompiled firmware under every sketch), downloads it and installs it.
//
// The server holds, side by side:
//   baseline-version.txt          the newest Baseline version, e.g. 0.2.0
//   baseline-<target>.bsu         the update for each kind of board; flick
//                                 baseline build writes dist/<target>/baseline-<target>.bsu
//   ota_pull_baseline.flick         this sketch (build/<target>/ota_pull_baseline.flick)
//
// Boards with 8 or 16 MB of flash keep this sketch: the new Baseline goes into
// the second Baseline slot, and the old one comes back if it fails to start.
// On 4 MB boards (Pro Micro C3, Qwiic Pocket C6, Thing Plus S3) the update
// erases the sketch and LittleFS, so Baseline installs this sketch again from
// the server afterwards (the second argument of baselineFromUrl).
//
// Try it with your PC as the server, on the same network as the board:
//   1. Set UPDATE_URL below to http://<your PC's IP>:8000 and upload this sketch over USB.
//   2. Into one folder, copy dist/<target>/baseline-<target>.bsu, this
//      sketch's .flick, and a baseline-version.txt newer than the version the
//      board prints. Serve the folder: python -m http.server 8000
// Within a minute the board downloads the update (about 1.3 MB), restarts,
// and prints the new version.
//
// WiFi: WIFI_SSID / WIFI_PASSWORD from secrets.cfg (see secrets.cfg.example).
#include <WiFi.h>
#include <HTTPClient.h>

#ifndef UPDATE_URL
#define UPDATE_URL "http://192.168.1.100:8000"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif

const unsigned long CHECK_EVERY_MS = 60 * 1000;
unsigned long lastCheck;

// "0.10.2" > "0.9.7": compare each number in turn.
int compareVersions(const char *a, const char *b)
{
    for (int part = 0; part < 4; part++) {
        char *ea, *eb;
        long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        a = *ea == '.' ? ea + 1 : ea;
        b = *eb == '.' ? eb + 1 : eb;
    }
    return 0;
}

void checkForUpdate()
{
    HTTPClient http;
    http.begin(UPDATE_URL "/baseline-version.txt");
    int code = http.GET();
    String latest = code == 200 ? http.getString() : String();
    http.end();
    latest.trim();
    if (!latest.length()) {
        Serial.printf("version check failed: HTTP %d\n", code);
        return;
    }
    const char *running = BaselineUpdate.baselineVersion();
    if (compareVersions(latest.c_str(), running) <= 0) {
        Serial.printf("Baseline %s is up to date\n", running);
        return;
    }
    Serial.printf("installing Baseline %s (now %s, update mode %s)...\n", latest.c_str(), running,
                  BaselineUpdate.modeName());
    int err = BaselineUpdate.baselineFromUrl(UPDATE_URL "/baseline-" BL_TARGET_NAME ".bsu",
                                             UPDATE_URL "/ota_pull_baseline.flick");    // restarts when it works
    Serial.printf("update failed (%d); trying again later\n", err);
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.printf("running on Baseline %s\n", BaselineUpdate.baselineVersion());
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
