// idf_test.ino - the ESP-IDF calls of ABI 1.10 (ESP32 family). One PASS/FAIL
// line per check, then a RESULT line.
//
//   - sscanf, strtok_r, strcasestr, strnstr, String::toCharArray, IPAddress("...")
//   - esp_read_mac, esp_err_to_name, esp_partition_find/get/next
//   - heap_caps_malloc/realloc in PSRAM and internal RAM, heap integrity
//   - mbedTLS certificate and key checks (a test pair made with openssl)
//   - Update refuses an image that isn't a sketch or an app
//   - setRxFIFOFull, base64::encode
//   - disableLoopWDT(): a 7 s busy wait in loop() doesn't trip the watchdog
//   - RTC_NOINIT_ATTR: a value survives ESP.restart() (the sketch restarts once)
#if !defined(BL_PLATFORM_ESP32)
#error "This board has no ESP-IDF calls."
#endif
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/ctr_drbg.h>
#include <Update.h>
#include <base64.h>

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

#define NOINIT_MAGIC 0x464C4B31     // "FLK1"
RTC_NOINIT_ATTR uint32_t noinitMagic;
RTC_NOINIT_ATTR uint32_t noinitCount;

static const char testCert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBfzCCASWgAwIBAgIUcGhxdp2WDMm/HjZIVV014IuGh60wCgYIKoZIzj0EAwIw\n"
    "FTETMBEGA1UEAwwKZmxpY2stdGVzdDAeFw0yNjExMTkyMjE4NDBaFw0zNjExMTYy\n"
    "MjE4NDBaMBUxEzARBgNVBAMMCmZsaWNrLXRlc3QwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAATyUzg7Pv1780zXxv6Kd27YAADDuHOB96F4kWbHhUvKxFNS4UGNC+tu\n"
    "HqMBnu6ikJlVgv1UcOGAZXZpKUYeUHCMo1MwUTAdBgNVHQ4EFgQU3ZdI51pcM39Z\n"
    "gIvNI/tFFqoaCZcwHwYDVR0jBBgwFoAU3ZdI51pcM39ZgIvNI/tFFqoaCZcwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNIADBFAiEAo8ajwNSWvC5aBj+DFXlA\n"
    "AWvYAl7Hhumhvj/uMyYh1KsCIEk43RANBS6aeFWwwMkNF9oAABwRnmnFRS88BIOO\n"
    "SafJ\n"
    "-----END CERTIFICATE-----\n";
static const char testKey[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgwk2VBu/VL3A4//MD\n"
    "SFlHcrJkneHwphSKgCYLUNVii5ChRANCAATyUzg7Pv1780zXxv6Kd27YAADDuHOB\n"
    "96F4kWbHhUvKxFNS4UGNC+tuHqMBnu6ikJlVgv1UcOGAZXZpKUYeUHCM\n"
    "-----END PRIVATE KEY-----\n";

static bool inPsram(const void *p)
{
    return (uint32_t)p >= 0x3F800000 && (uint32_t)p < 0x3FC00000;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("idf_test: start");

    // ---- RTC_NOINIT_ATTR: restart once, then check the value came back ----
    if (noinitMagic == NOINIT_MAGIC && noinitCount == 1) {
        noinitCount = 2;
        CHECK("RTC_NOINIT_ATTR value survives ESP.restart()", true);
    } else {
        noinitMagic = NOINIT_MAGIC;
        noinitCount = 1;
        Serial.println("idf_test: restarting once to check RTC_NOINIT_ATTR");
        delay(100);
        ESP.restart();
    }

    // ---- C library and Arduino helpers ----
    int i = 0; float f = 0; char word[8] = "";
    CHECK("sscanf %d %f %s", sscanf("12 3.5 abc", "%d %f %7s", &i, &f, word) == 3 && i == 12 && f == 3.5f && !strcmp(word, "abc"));
    char list[] = "a,b,,c", *save = nullptr;
    const char *t1 = strtok_r(list, ",", &save), *t2 = strtok_r(nullptr, ",", &save), *t3 = strtok_r(nullptr, ",", &save);
    CHECK("strtok_r", t1 && t2 && t3 && !strcmp(t1, "a") && !strcmp(t2, "b") && !strcmp(t3, "c") && !strtok_r(nullptr, ",", &save));
    CHECK("strcasestr", strcasestr("$GNGGA,abc", "gga") != nullptr && !strcasestr("abc", "x"));
    CHECK("strnstr", strnstr("abcdef", "cd", 4) && !strnstr("abcdef", "cd", 3));
    char buf[4];
    String("hello").toCharArray(buf, sizeof(buf));
    CHECK("String::toCharArray truncates", !strcmp(buf, "hel"));
    CHECK("IPAddress(\"192.168.1.20\")", IPAddress("192.168.1.20")[3] == 20);
    CHECK("base64::encode", base64::encode(String("hello")) == "aGVsbG8=");
    char u64[24];
    snprintf(u64, sizeof(u64), "%" PRIu64, (uint64_t)12345678901ULL);
    CHECK("PRIu64", !strcmp(u64, "12345678901"));

    // ---- MAC addresses, error names ----
    uint8_t sta[6], bt[6];
    CHECK("esp_read_mac", esp_read_mac(sta, ESP_MAC_WIFI_STA) == ESP_OK && esp_read_mac(bt, ESP_MAC_BT) == ESP_OK &&
                              bt[5] == (uint8_t)(sta[5] + 2));
    CHECK("esp_err_to_name", !strcmp(esp_err_to_name(ESP_ERR_NO_MEM), "ESP_ERR_NO_MEM"));

    // ---- partition table ----
    int apps = 0;
    for (esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr); it;
         it = esp_partition_next(it))
        apps++;
    const esp_partition_t *fs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "userfs");
    CHECK("esp_partition_find: app slots", apps >= 1);
    CHECK("esp_partition_find_first: userfs", fs && fs->size > 0 && !strcmp(fs->label, "userfs"));

    // ---- heap_caps ----
    void *ps = heap_caps_malloc(10000, MALLOC_CAP_SPIRAM);
    void *in = heap_caps_malloc(1000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (psramFound()) {
        CHECK("heap_caps_malloc(SPIRAM) is in PSRAM", ps && inPsram(ps));
        ps = heap_caps_realloc(ps, 20000, MALLOC_CAP_SPIRAM);
        CHECK("heap_caps_realloc(SPIRAM)", ps && inPsram(ps));
    }
    CHECK("heap_caps_malloc(INTERNAL) is internal", in && !inPsram(in));
    heap_caps_free(ps);
    heap_caps_free(in);
    CHECK("heap_caps_get_largest_free_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > 10000);
    CHECK("heap_caps_check_integrity_all", heap_caps_check_integrity_all(true));

    // ---- mbedTLS checks ----
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    CHECK("mbedtls_x509_crt_parse: good", mbedtls_x509_crt_parse(&crt, (const unsigned char *)testCert, sizeof(testCert)) == 0);
    CHECK("mbedtls_x509_crt_parse: bad", mbedtls_x509_crt_parse(&crt, (const unsigned char *)"junk", 5) < 0);
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_context pk;
    mbedtls_ctr_drbg_context rng;
    mbedtls_pk_init(&pk);
    mbedtls_ctr_drbg_init(&rng);
    CHECK("mbedtls_pk_parse_key: good", mbedtls_pk_parse_key(&pk, (const unsigned char *)testKey, sizeof(testKey), nullptr, 0,
                                                             mbedtls_ctr_drbg_random, &rng) == 0);
    CHECK("mbedtls_pk_parse_key: bad", mbedtls_pk_parse_key(&pk, (const unsigned char *)"junk", 5, nullptr, 0,
                                                            mbedtls_ctr_drbg_random, &rng) < 0);
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&rng);

    // ---- Update: an image that is neither a sketch nor an app is refused ----
    uint8_t junk[16] = {1, 2, 3, 4};
    bool began = Update.begin(sizeof(junk));
    size_t wrote = Update.write(junk, sizeof(junk));
    CHECK("Update refuses a bad image", began && wrote == 0 && Update.hasError());
    Serial.printf("      (%s)\n", Update.errorString());

    // ---- UART receive threshold, task watchdog status ----
    Serial1.begin(115200);
    CHECK("Serial1.setRxFIFOFull(50)", Serial1.setRxFIFOFull(50));
    Serial1.end();
    CHECK("esp_task_wdt_status(sketch task)", esp_task_wdt_status(nullptr) == ESP_OK);

    disableLoopWDT();
    Serial.println("idf_test: loop() busy-waits 7 s with the loop watchdog off");
}

void loop()
{
    static bool done;
    if (done) {
        delay(1000);
        return;
    }
    uint32_t start = millis();
    while (millis() - start < 7000) {
        // busy: no delay(), no return
    }
    enableLoopWDT();
    CHECK("7 s in loop() after disableLoopWDT()", true);
    Serial.printf("RESULT %d passed, %d failed\n", passed, failed);
    done = true;
}
