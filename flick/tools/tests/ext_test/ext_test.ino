// ext_test.ino - Baseline extensions (bl_ext_find, ABI 1.10) with tools/tests/ext_demo.
// On a stock Baseline the extension is missing and the test says so (SKIP);
// on one built with --ext flick/tools/tests/ext_demo every check runs.
// One PASS/FAIL/SKIP line per check, then a RESULT line.
#include "ext_demo_api.h"

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("ext_test: start");
    CHECK("bl_ext_find(NULL) is NULL", bl_ext_find(NULL) == NULL);
    CHECK("bl_ext_find of an unknown name is NULL", bl_ext_find("no.such.extension") == NULL);
    const ext_demo_api_t *demo = (const ext_demo_api_t *)bl_ext_find(EXT_DEMO_NAME);
    if (!demo) {
        Serial.println("SKIP  " EXT_DEMO_NAME ": this Baseline was built without ext_demo");
    } else {
        CHECK("ext_demo version 1", demo->version == 1);
        CHECK("ext_demo add(2, 3)", demo->add(2, 3) == 5);
        const char *p = demo->platform();
        Serial.printf("      platform: %s, internal free: %u\n", p, (unsigned)demo->internal_free());
        CHECK("ext_demo platform text", p && p[0]);
    }
    Serial.printf("RESULT %d passed, %d failed\n", passed, failed);
}

void loop()
{
    delay(1000);
}
