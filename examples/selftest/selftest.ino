// selftest.ino - bring-up checks for a new board or Baseline build.
// Upload it, then read the console: one PASS/FAIL line per check and a
// RESULT line. Needs WiFi networks and BLE devices nearby for the last two.
// Boards without PSRAM skip the PSRAM checks (they check ps_malloc returns NULL).
#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>
#include <Ticker.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <time.h>

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

// External RAM on the data bus: classic ESP32 0x3F800000-0x3FC00000, ESP32-S3 0x3C000000-0x3E000000,
// ESP32-C5 0x42000000-0x44000000 (heap pointers on the other chips never fall in these ranges).
static bool in_psram(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    return (a >= 0x3F800000 && a < 0x3FC00000) || (a >= 0x3C000000 && a < 0x3E000000) ||
           (a >= 0x42000000 && a < 0x44000000);
}

Ticker ticker;
volatile uint32_t tick_count;
void on_tick() { tick_count++; }

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n--- selftest ---");

    // PSRAM (skipped on boards without it)
    uint32_t ps = ESP.getPsramSize();
    Serial.printf("psram size %u, free %u\n", (unsigned)ps, (unsigned)ESP.getFreePsram());
    void *m10k = malloc(10000), *m100 = malloc(100);
    Serial.printf("malloc(10000)=%p malloc(100)=%p\n", m10k, m100);
    CHECK("malloc(10000) and malloc(100)", m10k && m100);
    CHECK("malloc(100) stays internal", m100 && !in_psram(m100));
    if (!psramFound()) {
        CHECK("no PSRAM: ps_malloc returns NULL", ps_malloc(1000) == nullptr);
        Serial.println("SKIP  PSRAM checks (this board has no PSRAM)");
    } else {
        CHECK("PSRAM is 2 MB or more", ps >= 2 * 1024 * 1024 - 65536);
        CHECK("malloc(10000) goes to PSRAM", in_psram(m10k));
        uint8_t *big = (uint8_t *)ps_malloc(200000);
        CHECK("ps_malloc(200000) lands in PSRAM", big && in_psram(big));
        if (big) {
            for (uint32_t i = 0; i < 200000; i++) big[i] = (uint8_t)(i * 7 + 3);
            bool same = true;
            for (uint32_t i = 0; i < 200000; i++) same &= big[i] == (uint8_t)(i * 7 + 3);
            CHECK("200 KB PSRAM write/read pattern", same);
            free(big);
        }
        void *pc = ps_calloc(1000, 4);
        bool zero = pc != nullptr;
        for (int i = 0; pc && i < 4000; i++) zero &= ((uint8_t *)pc)[i] == 0;
        CHECK("ps_calloc zeroed in PSRAM", zero && in_psram(pc));
        pc = ps_realloc(pc, 50000);
        CHECK("ps_realloc", pc && in_psram(pc));
        free(pc);
    }
    free(m10k); free(m100);

    // Strings / numbers
    CHECK("String(12345)", String(12345) == "12345");
    CHECK("String(-7)", String(-7) == "-7");
    CHECK("String(3.5, 2)", String(3.5, 2) == "3.50");
    randomSeed(1);
    long r = random(1000);
    CHECK("random(1000) in range", r >= 0 && r < 1000);

    // Time (SDK)
    struct tm tm = {};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 9; tm.tm_mday = 21; tm.tm_hour = 12;
    time_t t = mktime(&tm);
    struct tm back;
    gmtime_r(&t, &back);
    CHECK("mktime/gmtime round trip", back.tm_year == 126 && back.tm_mon == 9 && back.tm_mday == 21 && back.tm_hour == 12);

    // Preferences (NVS)
    Preferences prefs;
    prefs.begin("selftest");
    uint32_t n = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", n);
    CHECK("Preferences write/read", prefs.getUInt("boots", 0) == n);
    prefs.putString("s", "hello");
    CHECK("Preferences string", prefs.getString("s") == "hello");
    prefs.end();
    Serial.printf("boots counted in NVS: %u\n", (unsigned)n);

    // LittleFS on the resized partition
    bool mounted = LittleFS.begin(true);
    CHECK("LittleFS mount (format if needed)", mounted);
    if (mounted) {
        Serial.printf("LittleFS total %u used %u\n", (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
        CHECK("LittleFS has over 1 MB", LittleFS.totalBytes() > 1000000);
        File f = LittleFS.open("/t.txt", FILE_WRITE);
        f.print("baseline idf");
        f.close();
        f = LittleFS.open("/t.txt");
        String s = f.readString();
        f.close();
        CHECK("LittleFS write/read", s == "baseline idf");
    }

    // Timers
    tick_count = 0;
    ticker.attach_ms(50, on_tick);
    delay(1000);
    ticker.detach();
    Serial.printf("ticker fired %u times in 1 s (expect ~20)\n", (unsigned)tick_count);
    CHECK("Ticker 50 ms", tick_count >= 17 && tick_count <= 22);

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int nets = WiFi.scanNetworks();
    Serial.printf("WiFi scan: %d networks\n", nets);
    CHECK("WiFi scan", nets > 0);
    WiFi.scanDelete();

    // BLE
    BLEDevice::init("selftest");
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults *found = scan->start(3, false);
    int ble = found ? found->getCount() : -1;
    Serial.printf("BLE scan: %d devices\n", ble);
    CHECK("BLE scan", ble >= 0);
    scan->clearResults();

    Serial.printf("heap free %u, psram free %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    Serial.printf("RESULT: %d passed, %d failed\n", passed, failed);
}

void loop() { delay(1000); }
