/* sketch_net.cpp - out-of-line parts of baseline_net.h. */
#include "baseline.h"

#if defined(BL_HAS_WIFI)     /* WiFi, mDNS, OTA: WiFi boards */

WiFiClass WiFi;
MDNSResponder MDNS;
ArduinoOTAClass ArduinoOTA;

String MDNSResponder::hostname(int idx)
{
    char buf[64];
    return bl_mdns_result(idx, BL_MDNS_HOSTNAME, buf, sizeof(buf)) >= 0 ? String(buf) : String();
}

/* ---- WiFi ---- */

String WiFiClass::macAddress()
{
    uint8_t m[6];
    char text[18];
    bl_wifi_mac(m);
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(text);
}

String WiFiClass::SSID()
{
    char buf[33];
    bl_wifi_ssid(buf, sizeof(buf));
    return String(buf);
}

String WiFiClass::SSID(uint8_t i)
{
    bl_wifi_ap_t ap;
    return scanResult(i, ap) ? String(ap.ssid) : String();
}

String WiFiClass::BSSIDstr(uint8_t i)
{
    bl_wifi_ap_t ap;
    if (!scanResult(i, ap)) {
        return String();
    }
    char text[18];
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    return String(text);
}

#endif /* BL_HAS_WIFI */

#if defined(BL_HAS_NET)      /* sockets, HTTP: any network */

/* ---- WiFiClient ---- */

int WiFiClient::connect(const char *host, uint16_t port, int32_t timeout_ms)
{
    if (h_ < 0) {
        h_ = bl_tcp_client_new(isSecure());
        if (h_ < 0) {
            return 0;
        }
    }
    if (isSecure()) {
        configure();
    }
    return bl_tcp_client_connect(h_, host, port, timeout_ms);
}

int WiFiClient::read()
{
    uint8_t b;
    return (h_ >= 0 && bl_tcp_client_read(h_, &b, 1) == 1) ? b : -1;
}

IPAddress WiFiClient::remoteIP()
{
    uint32_t ip = 0;
    if (h_ >= 0) {
        bl_tcp_client_remote(h_, &ip, 0);
    }
    return IPAddress(ip);
}

uint16_t WiFiClient::remotePort()
{
    uint16_t port = 0;
    if (h_ >= 0) {
        bl_tcp_client_remote(h_, 0, &port);
    }
    return port;
}

/* ---- WiFiUDP ---- */

int WiFiUDP::read()
{
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
}

int WiFiUDP::read(uint8_t *buf, size_t size)
{
    if (h_ < 0 || remaining_ <= 0) {
        return -1;
    }
    int n = bl_udp_read(h_, buf, size);
    if (n > 0) {
        remaining_ -= n;
    }
    return n;
}

IPAddress WiFiUDP::remoteIP()
{
    uint32_t ip = 0;
    if (h_ >= 0) {
        bl_udp_remote(h_, &ip, 0);
    }
    return IPAddress(ip);
}

uint16_t WiFiUDP::remotePort()
{
    uint16_t port = 0;
    if (h_ >= 0) {
        bl_udp_remote(h_, 0, &port);
    }
    return port;
}

/* ---- HTTPClient ---- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void HTTPClient::setAuthorization(const char *user, const char *password)
{
    String plain = String(user) + ":" + password;
    String enc;
    const uint8_t *p = (const uint8_t *)plain.c_str();
    size_t n = plain.length();
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        enc += B64[(v >> 18) & 63];
        enc += B64[(v >> 12) & 63];
        enc += i + 1 < n ? B64[(v >> 6) & 63] : '=';
        enc += i + 2 < n ? B64[v & 63] : '=';
    }
    addHeader("Authorization", "Basic " + enc);
}

int HTTPClient::sendRequest(const char *method, const uint8_t *payload, size_t size)
{
    char *response = 0;
    size_t len = 0;
    int r = bl_http_request(method, url_.c_str(), headers_.c_str(), payload, size, &response, &len);
    body_ = response ? String(response, len) : String();
    bl_free(response);
    switch (r) {
    case BL_ERR_STATE:   return HTTPC_ERROR_NOT_CONNECTED;
    case BL_ERR_TIMEOUT: return HTTPC_ERROR_READ_TIMEOUT;
    case BL_ERR_NO_MEM:  return HTTPC_ERROR_TOO_LESS_RAM;
    default:             return r < 0 ? HTTPC_ERROR_CONNECTION_REFUSED : r;
    }
}

String HTTPClient::errorToString(int error)
{
    switch (error) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "connection refused";
    case HTTPC_ERROR_NOT_CONNECTED:      return "not connected";
    case HTTPC_ERROR_TOO_LESS_RAM:       return "too less ram";
    case HTTPC_ERROR_READ_TIMEOUT:       return "read Timeout";
    default:                             return String();
    }
}

#endif /* BL_HAS_NET */

#if defined(BL_HAS_WIFI)     /* the web server: WiFi boards for now */

/* ---- WebServer ---- */

static void call_handler(void *arg)
{
    ((WebServer::THandlerFunction)arg)();
}

void WebServer::on(const char *uri, HTTPMethod method, THandlerFunction fn)
{
    ensure();
    if (h_ >= 0) {
        bl_web_on(h_, uri, method, call_handler, (void *)fn);
    }
}

void WebServer::onNotFound(THandlerFunction fn)
{
    ensure();
    if (h_ >= 0) {
        bl_web_on_not_found(h_, call_handler, (void *)fn);
    }
}

String WebServer::text(int what, int index, const char *name)
{
    if (h_ < 0) {
        return String();
    }
    char small[128];
    int n = bl_web_request(h_, what, index, name, small, sizeof(small));
    if (n < 0) {
        return String();
    }
    if (n < (int)sizeof(small)) {
        return String(small, n);
    }
    char *big = (char *)malloc(n + 1);
    if (!big) {
        return String();
    }
    bl_web_request(h_, what, index, name, big, n + 1);
    String s(big, n);
    free(big);
    return s;
}

#endif /* BL_HAS_WIFI */
