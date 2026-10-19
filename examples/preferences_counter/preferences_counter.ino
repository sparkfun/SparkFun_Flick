// preferences_counter.ino - count restarts in NVS with Preferences.
// The count survives power cycles and sketch uploads. Press RESET to see it grow.
#include <Preferences.h>

Preferences prefs;

void setup()
{
    Serial.begin(115200);
    prefs.begin("counter");
    unsigned int count = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", count);
    prefs.putString("last", "preferences_counter");
    Serial.printf("This sketch has started %u times (last writer: %s)\n", count, prefs.getString("last").c_str());
    prefs.end();
}

void loop()
{
    delay(1000);
}
