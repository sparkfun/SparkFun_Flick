/* API table: mDNS (ESPmDNS). Also used by OTA to advertise NAME.local. */
#include <Arduino.h>
#include <ESPmDNS.h>
#include "gen/bl_impl.h"
#include "bl_str.h"
#include "sketch_loader.h"
#include "bl_mdns.h"

static bool s_started;
static String s_host;

extern "C" int bl_impl_mdns_begin(const char *hostname)
{
    if (!hostname || !*hostname) {
        return BL_ERR_ARG;
    }
    if (s_started) {
        MDNS.end();
    }
    s_started = MDNS.begin(hostname);
    s_host = s_started ? hostname : "";
    return s_started ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_mdns_end(void)
{
    if (s_started) {
        MDNS.end();
        s_started = false;
        s_host = "";
    }
}

bool bl_mdns_ensure(const char *hostname)
{
    return s_started || bl_impl_mdns_begin(hostname) == BL_OK;
}

extern "C" int bl_impl_mdns_add_service(const char *service, const char *proto, uint16_t port)
{
    if (!s_started || !service || !proto) {
        return BL_ERR_STATE;
    }
    return MDNS.addService(service, proto, port) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_mdns_add_txt(const char *service, const char *proto, const char *key, const char *value)
{
    if (!s_started || !service || !proto || !key || !value) {
        return BL_ERR_STATE;
    }
    MDNS.addServiceTxt(service, proto, key, value);     // returns void in Arduino-ESP32 3.3
    return BL_OK;
}

extern "C" uint32_t bl_impl_mdns_query_host(const char *host, uint32_t timeout_ms)
{
    if (!host) {
        return 0;
    }
    if (!s_started) {
        bl_mdns_ensure("baseline");
    }
    WdtPause wdt;
    return (uint32_t)MDNS.queryHost(host, timeout_ms ? timeout_ms : 2000);
}

extern "C" int bl_impl_mdns_query_service(const char *service, const char *proto)
{
    if (!service || !proto) {
        return BL_ERR_ARG;
    }
    if (!s_started) {
        bl_mdns_ensure("baseline");
    }
    WdtPause wdt;
    return MDNS.queryService(service, proto);
}

extern "C" int bl_impl_mdns_result(int index, int what, char *buf, size_t size)
{
    switch (what) {
    case BL_MDNS_HOSTNAME: {
        String h = MDNS.hostname(index);
        if (buf && size) {
            bl_copy_str(buf, size, h);
        }
        return (int)h.length();
    }
    case BL_MDNS_IP:   return (int)(uint32_t)MDNS.address(index);
    case BL_MDNS_PORT: return MDNS.port(index);
    default:           return -1;
    }
}
