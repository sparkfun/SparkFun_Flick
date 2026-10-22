/* API table: Bluetooth Classic serial port (SPP, Arduino's BluetoothSerial).
 * Only chips with Bluetooth Classic have it (the classic ESP32). Elsewhere the
 * same table entries exist and report BL_ERR_UNSUPPORTED. */
#include <Arduino.h>
#include "sdkconfig.h"
#include "gen/bl_impl.h"
#include "sketch_loader.h"
#include "bl_console.h"

#if defined(CONFIG_BT_SPP_ENABLED)
#include <BluetoothSerial.h>

static BluetoothSerial *s_bt;
static bool s_started;
static String s_pin;

extern "C" int bl_impl_bt_spp_begin(const char *name)
{
    if (!s_bt) {
        s_bt = new BluetoothSerial();
    }
    if (!s_started && btMemReleased(BT_MODE_CLASSIC_BT)) {
        // BLE started first and gave Bluetooth Classic's memory to the heap.
        BLOG_E("Bluetooth Classic is unavailable after BLE has started; restart, then begin SPP first");
        return BL_ERR_FAIL;
    }
    if (!s_started) {
        WdtPause wdt;   // starting the Bluetooth stack takes about a second
        s_started = s_bt->begin(String(name && *name ? name : "ESP32"));
        if (s_started && s_pin.length()) {
            s_bt->setPin(s_pin.c_str(), (uint8_t)s_pin.length());
        }
    }
    return s_started ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_bt_spp_end(void)
{
    if (s_started) {
        s_bt->end();
        s_started = false;
    }
}

extern "C" int bl_impl_bt_spp_connected(void)
{
    return s_started && s_bt->hasClient() ? 1 : 0;
}

extern "C" int bl_impl_bt_spp_write(const uint8_t *data, size_t len)
{
    return (s_started && data) ? (int)s_bt->write(data, len) : 0;
}

extern "C" int bl_impl_bt_spp_available(void)
{
    return s_started ? s_bt->available() : 0;
}

extern "C" int bl_impl_bt_spp_read(uint8_t *buf, size_t size)
{
    size_t n = 0;
    while (s_started && buf && n < size && s_bt->available()) {
        int c = s_bt->read();
        if (c < 0) {
            break;
        }
        buf[n++] = (uint8_t)c;
    }
    return (int)n;
}

extern "C" int bl_impl_bt_spp_peek(void)
{
    return s_started ? s_bt->peek() : -1;
}

extern "C" void bl_impl_bt_spp_flush(void)
{
    if (s_started) {
        s_bt->flush();
    }
}

extern "C" int bl_impl_bt_spp_set_pin(const char *pin)
{
    if (!pin || strlen(pin) > 16) {
        return BL_ERR_ARG;
    }
    s_pin = pin;
    if (s_started) {
        s_bt->setPin(s_pin.c_str(), (uint8_t)s_pin.length());
    }
    return BL_OK;
}

#else   /* no Bluetooth Classic on this chip */

extern "C" int bl_impl_bt_spp_begin(const char *name) { return BL_ERR_UNSUPPORTED; }
extern "C" void bl_impl_bt_spp_end(void) {}
extern "C" int bl_impl_bt_spp_connected(void) { return 0; }
extern "C" int bl_impl_bt_spp_write(const uint8_t *data, size_t len) { return 0; }
extern "C" int bl_impl_bt_spp_available(void) { return 0; }
extern "C" int bl_impl_bt_spp_read(uint8_t *buf, size_t size) { return 0; }
extern "C" int bl_impl_bt_spp_peek(void) { return -1; }
extern "C" void bl_impl_bt_spp_flush(void) {}
extern "C" int bl_impl_bt_spp_set_pin(const char *pin) { return BL_ERR_UNSUPPORTED; }

#endif
