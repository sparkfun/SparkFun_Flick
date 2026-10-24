/* pulseIn, WiFi events (WiFi.onEvent) and WiFiMulti (baseline_extras.h, baseline_net.h). */
#include "baseline.h"

/* ---- pulseIn ---- */

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    uint64_t start = micros();
    while (digitalRead(pin) == state) {         // wait for any previous pulse to end
        if (micros() - start > timeout) return 0;
    }
    while (digitalRead(pin) != state) {         // wait for the pulse to start
        if (micros() - start > timeout) return 0;
    }
    uint64_t begin = micros();
    while (digitalRead(pin) == state) {         // measure it
        if (micros() - start > timeout) return 0;
    }
    return (unsigned long)(micros() - begin);
}

/* ---- WiFi events: Baseline queues them; the pump delivers them on the sketch task ---- */

namespace {
const int MAX_HANDLERS = 8;
struct Handler {
    WiFiEventCb cb;
    WiFiEventSysCb cb2;
    arduino_event_id_t event;
};
Handler s_handlers[MAX_HANDLERS];
bool s_pump_added;

void wifi_event_pump()
{
    for (int e; (e = bl_wifi_event_take()) >= 0;) {
        for (Handler &h : s_handlers) {
            if (h.event != ARDUINO_EVENT_MAX && h.event != (arduino_event_id_t)e) {
                continue;
            }
            if (h.cb) {
                h.cb((arduino_event_id_t)e);
            } else if (h.cb2) {
                WiFiEventInfo_t info = {};
                h.cb2((arduino_event_id_t)e, info);
            }
        }
    }
}
}  // namespace

wifi_event_id_t bl_wifi_on_event(WiFiEventCb cb, WiFiEventSysCb cb2, arduino_event_id_t event)
{
    for (int i = 0; i < MAX_HANDLERS; i++) {
        if (!s_handlers[i].cb && !s_handlers[i].cb2) {
            s_handlers[i] = {cb, cb2, event};
            if (!s_pump_added) {
                s_pump_added = true;
                bl_wifi_event_take();       // makes Baseline start queueing events now
                bl_sketch_add_pump(wifi_event_pump);
            }
            return i + 1;
        }
    }
    return 0;
}

void bl_wifi_remove_event(wifi_event_id_t id)
{
    if (id >= 1 && id <= MAX_HANDLERS) {
        s_handlers[id - 1] = {};
    }
}

/* ---- WiFiMulti ---- */

bool WiFiMulti::addAP(const char *ssid, const char *password)
{
    if (!ssid || count_ >= MAX_APS) {
        return false;
    }
    ssid_[count_] = ssid;
    pass_[count_] = password;
    count_++;
    return true;
}

uint8_t WiFiMulti::run(uint32_t connectTimeout)
{
    if (WiFi.status() == WL_CONNECTED) {
        return WL_CONNECTED;
    }
    int n = WiFi.scanNetworks();
    int best = -1;
    int32_t best_rssi = -1000;
    for (int i = 0; i < n; i++) {
        String found = WiFi.SSID(i);
        for (int k = 0; k < count_; k++) {
            if (found == ssid_[k] && WiFi.RSSI(i) > best_rssi) {
                best = k;
                best_rssi = WiFi.RSSI(i);
            }
        }
    }
    WiFi.scanDelete();
    if (best < 0) {
        return WiFi.status();
    }
    WiFi.begin(ssid_[best], pass_[best]);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < connectTimeout) {
        delay(100);
    }
    return WiFi.status();
}
