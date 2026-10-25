// selftest.ino - bring-up checks for a new board or Baseline build.
// Upload it (or run `flick test`), then read the console: one PASS/FAIL/SKIP line
// per check and a RESULT line at the end.
//
//   - memory and PSRAM (PSRAM checks are skipped on boards without it)
//   - Strings, numbers, time functions
//   - Preferences (NVS) and LittleFS
//   - Ticker timing
//   - the board's own I2C devices (bl_board.h), and a Qwiic bus scan
//   - microSD, if the board has a slot and a card is in it
//   - the status and RGB LEDs (look at the board when it says LOOK)
//   - WiFi scan and BLE scan (needs networks / devices nearby)
//   - WiFi connection, NTP and HTTP, if WIFI_SSID is set (python flick.py wifi ...)
//   - deep sleep: the board sleeps for 2 s; after the timer wakes it, the
//     second phase checks that RTC memory and Preferences survived.
#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <Ticker.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Wire.h>
#include <time.h>

/* Kept in RTC memory across the deep sleep. */
struct Saved {
    uint32_t magic;
    int passed, failed, skipped;
    uint32_t nvs_value;
};
static const uint32_t MAGIC = 0x5E1F7E57;
static Saved *saved;

#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? saved->passed++ : saved->failed++; } while (0)
#define SKIP(name, why) do { Serial.printf("SKIP  %s (%s)\n", name, why); saved->skipped++; } while (0)

// External RAM on the data bus: classic ESP32 0x3F800000-0x3FC00000, ESP32-S3 0x3C000000-0x3E000000,
// ESP32-C5 0x42000000-0x44000000, ESP32-P4 0x48000000-0x4C000000 (heap pointers on the other chips
// never fall in these ranges).
static bool in_psram(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    return (a >= 0x3F800000 && a < 0x3FC00000) || (a >= 0x3C000000 && a < 0x3E000000) ||
           (a >= 0x42000000 && a < 0x44000000) || (a >= 0x48000000 && a < 0x4C000000);
}

Ticker ticker;
volatile uint32_t tick_count;
void on_tick() { tick_count++; }

static void check_memory()
{
    uint32_t ps = ESP.getPsramSize();
    Serial.printf("psram size %u, free %u\n", (unsigned)ps, (unsigned)ESP.getFreePsram());
    void *m10k = malloc(10000), *m100 = malloc(100);
    CHECK("malloc(10000) and malloc(100)", m10k && m100);
    CHECK("malloc(100) stays internal", m100 && !in_psram(m100));
    if (!psramFound()) {
        CHECK("no PSRAM: ps_malloc returns NULL", ps_malloc(1000) == nullptr);
        SKIP("PSRAM checks", "this board has no PSRAM");
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
    free(m10k);
    free(m100);
}

static void check_basics()
{
    CHECK("String(12345)", String(12345) == "12345");
    CHECK("String(-7)", String(-7) == "-7");
    CHECK("String(3.5, 2)", String(3.5, 2) == "3.50");
    CHECK("String from F()", String(F("flash")) == "flash");
    CHECK("<ctype.h> and Arduino character tests",
          isdigit('7') && !isdigit('a') && isalpha('Q') && isspace('\n') && isxdigit('F') && ispunct('!') &&
          toupper('q') == 'Q' && tolower('Z') == 'z' && isDigit('3') && !isUpperCase('a') && toUpperCase('b') == 'B');
    CHECK("sqrt/pow (libm)", fabs(sqrt(2.0) * sqrt(2.0) - 2.0) < 1e-9 && pow(2.0, 10) == 1024.0);
    long r = random(1000);
    CHECK("random(1000) in range", r >= 0 && r < 1000);
    struct tm tm = {};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 9; tm.tm_mday = 21; tm.tm_hour = 12;
    time_t t = mktime(&tm);
    struct tm back;
    gmtime_r(&t, &back);
    CHECK("mktime/gmtime round trip", back.tm_year == 126 && back.tm_mon == 9 && back.tm_mday == 21 && back.tm_hour == 12);
    float temp = temperatureRead();
    if (isnan(temp)) {
        SKIP("chip temperature", "no sensor on this chip");
    } else {
        Serial.printf("chip temperature %.1f C\n", temp);
        CHECK("chip temperature plausible", temp > -20 && temp < 100);
    }
    Serial.printf("reset reason %d\n", (int)esp_reset_reason());
}

static void check_storage()
{
    Preferences prefs;
    prefs.begin("selftest");
    saved->nvs_value = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", saved->nvs_value);
    CHECK("Preferences write/read", prefs.getUInt("boots", 0) == saved->nvs_value);
    prefs.putString("s", "hello");
    CHECK("Preferences string", prefs.getString("s") == "hello");
    prefs.end();

    bool mounted = LittleFS.begin(true);
    CHECK("LittleFS mount (format if needed)", mounted);
    if (mounted) {
        Serial.printf("LittleFS total %u used %u\n", (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
        CHECK("LittleFS has over 1 MB", LittleFS.totalBytes() > 1000000);
        File f = LittleFS.open("/t.txt", FILE_WRITE);
        f.print("baseline selftest");
        f.close();
        f = LittleFS.open("/t.txt");
        String s = f.readString();
        f.close();
        CHECK("LittleFS write/read", s == "baseline selftest");
    }

#if (defined(BL_BOARD_SD_CS) && BL_BOARD_SD_CS >= 0) || BL_SD_DEFAULT_BUS == 1
    bool sd = BL_SD_DEFAULT_BUS == 1 ? SD_MMC.begin() : SD.begin();
    if (!sd) {
        SKIP("microSD", "no card in the slot, or it could not be mounted");
    } else {
        uint64_t total = BL_SD_DEFAULT_BUS == 1 ? SD_MMC.totalBytes() : SD.totalBytes();
        Serial.printf("microSD %llu MB\n", (unsigned long long)(total >> 20));
        CHECK("microSD mounted", total > 0);
    }
#else
    SKIP("microSD", "this board has no slot");
#endif
}

static void check_timers()
{
    tick_count = 0;
    ticker.attach_ms(50, on_tick);
    delay(1000);
    ticker.detach();
    Serial.printf("ticker fired %u times in 1 s (expect ~20)\n", (unsigned)tick_count);
    CHECK("Ticker 50 ms", tick_count >= 17 && tick_count <= 22);
}

static void check_i2c()
{
    Wire.begin();
    Serial.print("Qwiic/I2C bus:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf(" 0x%02X", a);
            found++;
        }
    }
    Serial.printf(" (%d device%s)\n", found, found == 1 ? "" : "s");
#ifdef BL_BOARD_FUEL_GAUGE_ADDR
    Wire.beginTransmission(BL_BOARD_FUEL_GAUGE_ADDR);
    CHECK("on-board fuel gauge answers on I2C", Wire.endTransmission() == 0);
#else
    SKIP("on-board I2C device", "this board has none listed in bl_board.h");
#endif
#ifdef BL_BOARD_VBAT_ADC
    // Battery voltage through the board's divider (ADC check): a LiPo reads
    // 3.0-4.3 V; with no battery the charger's output floats near 4.2 V.
    int vbat = (int)(analogReadMilliVolts(BL_BOARD_VBAT_ADC) * BL_BOARD_VBAT_SCALE);
    Serial.printf("battery %d mV (ADC GPIO %d)\n", vbat, BL_BOARD_VBAT_ADC);
    CHECK("battery voltage via ADC plausible (3.0-4.4 V)", vbat >= 3000 && vbat <= 4400);
#endif
}

static void check_leds()
{
#if defined(RGB_BUILTIN)
    const bool plain_led = LED_BUILTIN != RGB_BUILTIN;     // some boards' only LED is the RGB one
#else
    const bool plain_led = true;
#endif
    if (plain_led) {
        Serial.println("LOOK  status LED blinks 3 times");
        pinMode(LED_BUILTIN, OUTPUT);
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(150);
            digitalWrite(LED_BUILTIN, LOW);
            delay(150);
        }
    }
#ifdef RGB_BUILTIN
    Serial.println("LOOK  RGB LED shows red, green, blue");
    rgbLedWrite(RGB_BUILTIN, 32, 0, 0);
    delay(300);
    rgbLedWrite(RGB_BUILTIN, 0, 32, 0);
    delay(300);
    rgbLedWrite(RGB_BUILTIN, 0, 0, 32);
    delay(300);
    rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
#endif
}

static void check_ble()
{
    if (!BLEDevice::init("selftest")) {
        SKIP("BLE scan", "this chip has no Bluetooth, or BLE could not start");
        return;
    }
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults *found = scan->start(3, false);
    int ble = found ? found->getCount() : -1;
    Serial.printf("BLE scan: %d devices\n", ble);
    CHECK("BLE scan", ble >= 0);
    scan->clearResults();
}

static void check_radios()
{
    if (WiFi.status() == WL_NO_SHIELD) {
        SKIP("WiFi scan, connect, NTP, HTTP", "this chip has no WiFi");
        check_ble();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int nets = WiFi.scanNetworks();
    Serial.printf("WiFi scan: %d networks\n", nets);
    CHECK("WiFi scan", nets > 0);
    WiFi.scanDelete();

#ifdef WIFI_SSID
    static volatile int got_ip;
    WiFi.onEvent([](WiFiEvent_t) { got_ip++; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(100);
    }
    bool up = WiFi.status() == WL_CONNECTED;
    CHECK("WiFi connects to " WIFI_SSID, up);
    delay(200);     // lets the event callback run
    CHECK("WiFi.onEvent saw GOT_IP", !up || got_ip > 0);
    if (up) {
        configTime(0, 0, "pool.ntp.org");
        struct tm now;
        CHECK("NTP time", getLocalTime(&now, 10000) && now.tm_year > 124);
        HTTPClient http;
        http.begin("http://example.com/");
        int code = http.GET();
        Serial.printf("HTTP GET example.com: %d\n", code);
        CHECK("HTTP GET", code == 200);
        http.end();
    }
    WiFi.disconnect();
#else
    SKIP("WiFi connect, NTP, HTTP", "no WIFI_SSID (python flick.py wifi NAME PASSWORD)");
#endif

    check_ble();
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    uint32_t size = 0;
    saved = (Saved *)bl_rtc_memory(&size);

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && saved->magic == MAGIC) {
        delay(3000);    // native-USB boards reappear on the computer after waking: give it time
        Serial.println("\n--- selftest, after deep sleep ---");
        CHECK("woke from deep sleep on the timer", true);
        CHECK("RTC memory kept through deep sleep", saved->magic == MAGIC);
        Preferences prefs;
        prefs.begin("selftest", true);
        CHECK("Preferences kept through deep sleep", prefs.getUInt("boots", 0) == saved->nvs_value);
        prefs.end();
    } else {
        Serial.println("\n--- selftest ---");
        memset(saved, 0, sizeof(*saved));
        check_memory();
        check_basics();
        check_storage();
        check_timers();
        check_i2c();
        check_leds();
        check_radios();
        saved->magic = MAGIC;
        Serial.println("deep sleep for 2 s...");
        delay(100);
        esp_sleep_enable_timer_wakeup(2000000);
        esp_deep_sleep_start();
    }
    saved->magic = 0;
    Serial.printf("heap free %u, psram free %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void loop()
{
    // Repeated, so a console that reconnects late (after the deep sleep) still sees it.
    Serial.printf("RESULT: %d passed, %d failed, %d skipped\n", saved->passed, saved->failed, saved->skipped);
    delay(2000);
}
