/* API table: WiFi (station) and HTTP GET. */
#include <Arduino.h>
#include <WiFi.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "gen/bl_impl.h"
#include "sketch_loader.h"

static_assert(BL_WIFI_CONNECTED == WL_CONNECTED && BL_WIFI_DISCONNECTED == WL_DISCONNECTED &&
              BL_WIFI_NO_SSID == WL_NO_SSID_AVAIL && BL_WIFI_STOPPED == WL_STOPPED,
              "BL_WIFI_* must equal Arduino's wl_status_t");

/* ---- WiFi ---- */

extern "C" int bl_impl_wifi_begin(const char *ssid, const char *password)
{
    if (!ssid || !*ssid) {
        return BL_ERR_ARG;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    return BL_OK;
}

extern "C" int bl_impl_wifi_status(void)
{
    return (int)WiFi.status();
}

extern "C" void bl_impl_wifi_disconnect(void)
{
    WiFi.disconnect();
}

extern "C" int bl_impl_wifi_local_ip(char *buf, size_t size)
{
    if (!buf || !size) {
        return BL_ERR_ARG;
    }
    if (WiFi.status() != WL_CONNECTED) {
        buf[0] = '\0';
        return BL_ERR_STATE;
    }
    strlcpy(buf, WiFi.localIP().toString().c_str(), size);
    return BL_OK;
}

extern "C" int bl_impl_wifi_rssi(void)
{
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

extern "C" int bl_impl_wifi_mac(uint8_t *mac)
{
    if (!mac) {
        return BL_ERR_ARG;
    }
    return esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK ? BL_OK : BL_ERR_FAIL;
}

/* ---- HTTP ---- */

struct http_body {
    char *buf;
    size_t size;
    size_t len;
};

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    http_body *body = (http_body *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int status = esp_http_client_get_status_code(evt->client);
        if (status >= 300 && status < 400) {
            return ESP_OK;      // body of a redirect; the client follows it
        }
        size_t room = body->size - 1 - body->len;
        size_t n = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
        memcpy(body->buf + body->len, evt->data, n);
        body->len += n;
    }
    return ESP_OK;
}

/* Growing response buffer for bl_impl_http_request. */
struct http_response {
    char *buf;
    size_t len;
    size_t cap;
    bool oom;
};

static esp_err_t http_request_event(esp_http_client_event_t *evt)
{
    http_response *r = (http_response *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || r->oom) {
        return ESP_OK;
    }
    int status = esp_http_client_get_status_code(evt->client);
    if (status >= 300 && status < 400) {
        return ESP_OK;      // body of a redirect; the client follows it
    }
    size_t need = r->len + evt->data_len + 1;
    if (need > r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 1024;
        while (cap < need) {
            cap *= 2;
        }
        char *grown = (char *)realloc(r->buf, cap);
        if (!grown) {
            r->oom = true;
            return ESP_OK;
        }
        r->buf = grown;
        r->cap = cap;
    }
    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

static bool http_method(const char *name, esp_http_client_method_t *out)
{
    static const struct { const char *name; esp_http_client_method_t method; } methods[] = {
        {"GET", HTTP_METHOD_GET},     {"POST", HTTP_METHOD_POST},     {"PUT", HTTP_METHOD_PUT},
        {"PATCH", HTTP_METHOD_PATCH}, {"DELETE", HTTP_METHOD_DELETE}, {"HEAD", HTTP_METHOD_HEAD},
    };
    for (const auto &m : methods) {
        if (strcasecmp(name, m.name) == 0) {
            *out = m.method;
            return true;
        }
    }
    return false;
}

/* headers: "Name: value\r\n" lines. */
static void http_set_headers(esp_http_client_handle_t client, const char *headers)
{
    if (!headers || !*headers) {
        return;
    }
    char *copy = strdup(headers);
    if (!copy) {
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(copy, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = '\0';
        char *value = colon + 1;
        while (*value == ' ') {
            value++;
        }
        esp_http_client_set_header(client, line, value);
    }
    free(copy);
}

extern "C" int bl_impl_http_request(const char *method, const char *url, const char *headers,
                                    const uint8_t *body, size_t body_len,
                                    char **response, size_t *response_len)
{
    if (response) {
        *response = NULL;
    }
    if (response_len) {
        *response_len = 0;
    }
    esp_http_client_method_t m;
    if (!url || !method || !http_method(method, &m)) {
        return BL_ERR_ARG;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return BL_ERR_STATE;
    }
    http_response r = {};
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = m;
    cfg.timeout_ms = 10000;
    cfg.event_handler = http_request_event;
    cfg.user_data = &r;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return BL_ERR_NO_MEM;
    }
    http_set_headers(client, headers);
    if (body && body_len) {
        esp_http_client_set_post_field(client, (const char *)body, (int)body_len);
    }
    esp_err_t err;
    {
        WdtPause wdt;   // up to 10 s
        err = esp_http_client_perform(client);
    }
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || r.oom) {
        free(r.buf);
        if (r.oom) {
            return BL_ERR_NO_MEM;
        }
        return (err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_TIMEOUT) ? BL_ERR_TIMEOUT : BL_ERR_FAIL;
    }
    if (!r.buf) {
        r.buf = (char *)calloc(1, 1);       // empty body: still hand back ""
    }
    if (response) {
        *response = r.buf;
    } else {
        free(r.buf);
    }
    if (response_len) {
        *response_len = r.len;
    }
    return status;
}

extern "C" int bl_impl_http_get(const char *url, char *buf, size_t size, int *status)
{
    if (!url || !buf || !size) {
        return BL_ERR_ARG;
    }
    buf[0] = '\0';
    if (status) {
        *status = 0;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return BL_ERR_STATE;
    }
    http_body body = { buf, size, 0 };
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 10000;
    cfg.event_handler = http_event;
    cfg.user_data = &body;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return BL_ERR_NO_MEM;
    }
    esp_err_t err;
    {
        WdtPause wdt;   // up to 10 s
        err = esp_http_client_perform(client);
    }
    if (status) {
        *status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    buf[body.len] = '\0';
    if (err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_TIMEOUT) {
        return BL_ERR_TIMEOUT;
    }
    return err == ESP_OK ? (int)body.len : BL_ERR_FAIL;
}

/* ---- WiFi: modes, access point, scan, addressing (ABI 1.3) ---- */

static_assert(BL_WIFI_MODE_OFF == WIFI_MODE_NULL && BL_WIFI_MODE_STA == WIFI_MODE_STA &&
              BL_WIFI_MODE_AP == WIFI_MODE_AP && BL_WIFI_MODE_AP_STA == WIFI_MODE_APSTA,
              "BL_WIFI_MODE_* must equal wifi_mode_t");

extern "C" int bl_impl_wifi_set_mode(int mode)
{
    if (mode < BL_WIFI_MODE_OFF || mode > BL_WIFI_MODE_AP_STA) {
        return BL_ERR_ARG;
    }
    return WiFi.mode((wifi_mode_t)mode) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_get_mode(void)
{
    return (int)WiFi.getMode();
}

extern "C" int bl_impl_wifi_softap(const char *ssid, const char *password, int channel, int hidden, int max_clients)
{
    if (!ssid || !*ssid || (password && *password && strlen(password) < 8)) {
        return BL_ERR_ARG;
    }
    bool ok = WiFi.softAP(ssid, password && *password ? password : NULL, channel > 0 ? channel : 1,
                          hidden ? 1 : 0, max_clients > 0 ? max_clients : 4);
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_softap_stop(void)
{
    return WiFi.softAPdisconnect(true) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_softap_stations(void)
{
    return WiFi.softAPgetStationNum();
}

extern "C" uint32_t bl_impl_wifi_ip(int which)
{
    switch (which) {
    case BL_IP_STA:     return (uint32_t)WiFi.localIP();
    case BL_IP_GATEWAY: return (uint32_t)WiFi.gatewayIP();
    case BL_IP_SUBNET:  return (uint32_t)WiFi.subnetMask();
    case BL_IP_DNS:     return (uint32_t)WiFi.dnsIP();
    case BL_IP_SOFTAP:  return (uint32_t)WiFi.softAPIP();
    default:            return 0;
    }
}

extern "C" int bl_impl_wifi_scan(void)
{
    WdtPause wdt;
    int n = WiFi.scanNetworks();
    return n >= 0 ? n : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_scan_result(int index, bl_wifi_ap_t *ap)
{
    if (!ap || index < 0 || index >= WiFi.scanComplete()) {
        return BL_ERR_ARG;
    }
    memset(ap, 0, sizeof(*ap));
    strlcpy(ap->ssid, WiFi.SSID(index).c_str(), sizeof(ap->ssid));
    ap->rssi = (int8_t)WiFi.RSSI(index);
    ap->channel = (uint8_t)WiFi.channel(index);
    ap->auth = (uint8_t)WiFi.encryptionType(index);
    uint8_t *bssid = WiFi.BSSID(index);
    if (bssid) {
        memcpy(ap->bssid, bssid, 6);
    }
    return BL_OK;
}

extern "C" int bl_impl_wifi_channel(void)
{
    return WiFi.channel();
}

extern "C" int bl_impl_wifi_ssid(char *buf, size_t size)
{
    String s = WiFi.SSID();
    if (buf && size) {
        strlcpy(buf, s.c_str(), size);
    }
    return (int)s.length();
}

extern "C" int bl_impl_wifi_config(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns)
{
    return WiFi.config(IPAddress(ip), IPAddress(gateway), IPAddress(subnet), IPAddress(dns)) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_set_hostname(const char *name)
{
    return name && WiFi.setHostname(name) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_net_resolve(const char *host, uint32_t *ip)
{
    if (!host || !ip) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;
    IPAddress addr;
    if (WiFi.hostByName(host, addr) != 1) {
        return BL_ERR_FAIL;
    }
    *ip = (uint32_t)addr;
    return BL_OK;
}
