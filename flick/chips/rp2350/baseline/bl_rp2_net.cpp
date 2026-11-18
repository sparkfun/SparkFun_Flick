/*
 * bl_rp2_net.cpp - WiFi and networking for RP2350 boards with a CYW43 radio
 * (the Thing Plus RP2350's RM2), on arduino-pico's WiFi, HTTPClient, WebServer,
 * DNSServer, LEAmDNS and NTP. The same table entries as the ESP32 Baselines
 * (baseline/src/bl_impl_wifi.cpp, bl_impl_net.cpp): the sketch's WiFiClient,
 * HTTPClient, WebServer, ... wrap these by handle.
 *
 * Built only for boards whose bl_board.h defines BL_HAS_WIFI; elsewhere the
 * entries stay the weak "unsupported" ones and bl_net_poll() does nothing.
 *
 * TLS is BearSSL. The trust store is the Mozilla CA list the ESP32 Baselines
 * use (bl_ca_bundle.h, made by flick from chips/rp2350/cacrt_all.pem), kept in
 * flash as DER: BearSSL asks for one issuer at a time, and only that one is
 * decoded into RAM. TLS calls run on Baseline's big stack (bl_on_big_stack).
 */
#include "bl_rp2.h"

#if !defined(BL_HAS_WIFI)
extern "C" void bl_net_poll(void)
{
}
#else

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LEAmDNS.h>
#include <time.h>
#include <sys/time.h>
#include "bl_ca_bundle.h"

#define MAX_CLIENTS     12
#define MAX_SERVERS     4
#define MAX_UDP         4
#define MAX_WEB         2
#define HTTP_TIMEOUT_MS 10000

static_assert(BL_WIFI_CONNECTED == WL_CONNECTED && BL_WIFI_DISCONNECTED == WL_DISCONNECTED &&
              BL_WIFI_NO_SSID == WL_NO_SSID_AVAIL, "BL_WIFI_* must equal wl_status_t");
static_assert(BL_WIFI_MODE_OFF == WIFI_OFF && BL_WIFI_MODE_STA == WIFI_STA && BL_WIFI_MODE_AP == WIFI_AP &&
              BL_WIFI_MODE_AP_STA == WIFI_AP_STA, "BL_WIFI_MODE_* must equal WiFiMode_t");
static_assert(BL_HTTP_DELETE == HTTP_DELETE && BL_HTTP_GET == HTTP_GET && BL_HTTP_HEAD == HTTP_HEAD &&
              BL_HTTP_POST == HTTP_POST && BL_HTTP_PUT == HTTP_PUT && BL_HTTP_OPTIONS == HTTP_OPTIONS &&
              BL_HTTP_PATCH == HTTP_PATCH && BL_HTTP_ANY == HTTP_ANY, "BL_HTTP_* must equal HTTPMethod");

static int copy_text(const String &s, char *buf, size_t size)
{
    if (buf && size) {
        strlcpy(buf, s.c_str(), size);
    }
    return (int)s.length();
}

static bool clock_set(void)
{
    return time(NULL) > 1609459200;     // after 2021-01-01
}

/* ---- Trust store ------------------------------------------------------------
 * bl_ca_bundle: entries of [2-byte big-endian length][certificate DER]. The
 * index (SHA-256 of each subject name, as BearSSL hashes it) is made on the
 * first TLS connection. */
static void dn_append(void *ctx, const void *data, size_t len)
{
    br_sha256_update((br_sha256_context *)ctx, data, len);
}

class FlashCertStore : public BearSSL::CertStoreBase {
public:
    void installCertStore(br_x509_minimal_context *ctx) override
    {
        make_index();
        br_x509_minimal_set_dynamic(ctx, (void *)this, find, release);
    }

private:
    struct Entry {
        uint8_t sha256[32];
        const uint8_t *der;
        uint16_t len;
    };
    Entry *_index = nullptr;
    int _count = 0;
    BearSSL::X509List *_x509 = nullptr;

    void make_index()
    {
        if (_index) {
            return;
        }
        _index = (Entry *)malloc(sizeof(Entry) * BL_CA_BUNDLE_COUNT);
        br_x509_decoder_context *dec = new (std::nothrow) br_x509_decoder_context;
        if (!_index || !dec) {
            free(_index);
            _index = nullptr;
            delete dec;
            return;
        }
        const uint8_t *p = bl_ca_bundle, *end = bl_ca_bundle + sizeof(bl_ca_bundle);
        while (p + 2 <= end && _count < BL_CA_BUNDLE_COUNT) {
            uint16_t len = (uint16_t)(p[0] << 8 | p[1]);
            p += 2;
            br_sha256_context sha;
            br_sha256_init(&sha);
            br_x509_decoder_init(dec, dn_append, &sha, nullptr, nullptr);
            br_x509_decoder_push(dec, p, len);
            Entry &e = _index[_count++];
            br_sha256_out(&sha, e.sha256);
            e.der = p;
            e.len = len;
            p += len;
        }
        delete dec;
    }

    static const br_x509_trust_anchor *find(void *ctx, void *hashed_dn, size_t len)
    {
        FlashCertStore *cs = static_cast<FlashCertStore *>(ctx);
        if (len != 32 || !cs->_index) {
            return nullptr;
        }
        for (int i = 0; i < cs->_count; i++) {
            const Entry &e = cs->_index[i];
            if (memcmp(e.sha256, hashed_dn, 32) == 0) {
                delete cs->_x509;
                cs->_x509 = new (std::nothrow) BearSSL::X509List(e.der, e.len);
                if (!cs->_x509 || !cs->_x509->getCount()) {
                    return nullptr;
                }
                br_x509_trust_anchor *ta = (br_x509_trust_anchor *)cs->_x509->getTrustAnchors();
                memcpy(ta->dn.data, e.sha256, 32);      // BearSSL matches the hashed name
                ta->dn.len = 32;
                return ta;
            }
        }
        return nullptr;
    }

    static void release(void *ctx, const br_x509_trust_anchor *)
    {
        FlashCertStore *cs = static_cast<FlashCertStore *>(ctx);
        delete cs->_x509;
        cs->_x509 = nullptr;
    }
};
static FlashCertStore s_certs;

void bl_net_use_ca_bundle(WiFiClientSecure *c)
{
    c->setCertStore(&s_certs);
    if (clock_set()) {
        c->setX509Time(time(NULL));
    }
}
#define use_ca_bundle bl_net_use_ca_bundle

void bl_mqtt_poll(void);    /* bl_rp2_mqtt.cpp */

/* ---- Events for WiFi.onEvent (ABI 1.6) --------------------------------------
 * arduino-pico has no event system: Baseline watches the state from bl_poll()
 * and queues the Arduino-ESP32 event numbers the SDK knows. */
enum {
    EV_SCAN_DONE = 102, EV_STA_START = 110, EV_STA_STOP = 111, EV_STA_CONNECTED = 112,
    EV_STA_DISCONNECTED = 113, EV_STA_GOT_IP = 115, EV_STA_LOST_IP = 117,
    EV_AP_START = 130, EV_AP_STOP = 131, EV_AP_STACONNECTED = 132, EV_AP_STADISCONNECTED = 133,
};
static bool s_events_on;
static int s_ev[16];
static uint8_t s_ev_head, s_ev_tail;

static void event(int e)
{
    if (!s_events_on) {
        return;
    }
    uint8_t next = (uint8_t)((s_ev_head + 1) % 16);
    if (next != s_ev_tail) {        // full: dropped
        s_ev[s_ev_head] = e;
        s_ev_head = next;
    }
}

extern "C" int bl_impl_wifi_event_take(void)
{
    s_events_on = true;
    if (s_ev_tail == s_ev_head) {
        return -1;
    }
    int e = s_ev[s_ev_tail];
    s_ev_tail = (uint8_t)((s_ev_tail + 1) % 16);
    return e;
}

/* ---- WiFi -------------------------------------------------------------------- */
static bool s_ap_on;
static bool s_sta_started;

extern "C" int bl_impl_wifi_begin(const char *ssid, const char *password)
{
    if (!ssid || !*ssid) {
        return BL_ERR_ARG;
    }
    if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_STA);
    }
    BlWdogLong wdt;     // the radio's firmware loads on the first start
    WiFi.beginNoBlock(ssid, password && *password ? password : nullptr);
    s_sta_started = true;
    event(EV_STA_START);
    return BL_OK;
}

extern "C" int bl_impl_wifi_status(void)
{
    return (int)WiFi.status();
}

extern "C" void bl_impl_wifi_disconnect(void)
{
    WiFi.disconnect();
    if (s_sta_started) {
        s_sta_started = false;
        event(EV_STA_STOP);
    }
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
    copy_text(WiFi.localIP().toString(), buf, size);
    return BL_OK;
}

extern "C" int bl_impl_wifi_rssi(void)
{
    return WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0;
}

extern "C" int bl_impl_wifi_mac(uint8_t *mac)
{
    if (!mac) {
        return BL_ERR_ARG;
    }
    WiFi.macAddress(mac);
    return BL_OK;
}

extern "C" int bl_impl_wifi_set_mode(int mode)
{
    if (mode < BL_WIFI_MODE_OFF || mode > BL_WIFI_MODE_AP_STA) {
        return BL_ERR_ARG;
    }
    WiFi.mode((WiFiMode_t)mode);
    return BL_OK;
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
    BlWdogLong wdt;
    bool ok = WiFi.softAP(ssid, password && *password ? password : nullptr, channel > 0 ? channel : 1,
                          hidden ? 1 : 0, max_clients > 0 ? max_clients : 4);
    if (ok) {
        s_ap_on = true;
        event(EV_AP_START);
    }
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_softap_stop(void)
{
    bool ok = WiFi.softAPdisconnect(true);
    if (s_ap_on) {
        s_ap_on = false;
        event(EV_AP_STOP);
    }
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_wifi_softap_stations(void)
{
    return s_ap_on ? WiFi.softAPgetStationNum() : 0;
}

extern "C" uint32_t bl_impl_wifi_ip(int which)
{
    switch (which) {
    case BL_IP_STA:     return (uint32_t)WiFi.localIP();
    case BL_IP_GATEWAY: return (uint32_t)WiFi.gatewayIP();
    case BL_IP_SUBNET:  return (uint32_t)WiFi.subnetMask();
    case BL_IP_DNS:     return (uint32_t)WiFi.dnsIP();
    case BL_IP_SOFTAP:  return s_ap_on ? (uint32_t)WiFi.softAPIP() : 0;
    default:            return 0;
    }
}

extern "C" int bl_impl_wifi_scan(void)
{
    int n;
    {
        BlWdogLong wdt;     // a scan takes a few seconds
        n = WiFi.scanNetworks();
    }
    event(EV_SCAN_DONE);
    return n >= 0 ? n : BL_ERR_FAIL;
}

/* Encryption as Arduino-ESP32 reports it (wifi_auth_mode_t), which the SDK's
 * WiFi.encryptionType() hands back. */
static uint8_t auth_mode(uint8_t enc)
{
    switch (enc) {
    case ENC_TYPE_NONE: return 0;       // WIFI_AUTH_OPEN
    case ENC_TYPE_WEP:  return 1;
    case ENC_TYPE_TKIP: return 2;       // WPA_PSK
    case ENC_TYPE_CCMP: return 3;       // WPA2_PSK
    case ENC_TYPE_AUTO: return 4;       // WPA_WPA2_PSK
    case ENC_TYPE_GCMP: return 6;       // WPA3_PSK
    default:            return 3;
    }
}

extern "C" int bl_impl_wifi_scan_result(int index, bl_wifi_ap_t *ap)
{
    if (!ap || index < 0 || index >= WiFi.scanComplete()) {
        return BL_ERR_ARG;
    }
    memset(ap, 0, sizeof(*ap));
    const char *ssid = WiFi.SSID((uint8_t)index);
    strlcpy(ap->ssid, ssid ? ssid : "", sizeof(ap->ssid));
    ap->rssi = (int8_t)WiFi.RSSI((uint8_t)index);
    ap->channel = WiFi.channel((uint8_t)index);
    ap->auth = auth_mode(WiFi.encryptionType((uint8_t)index));
    WiFi.BSSID((uint8_t)index, ap->bssid);
    return BL_OK;
}

extern "C" int bl_impl_wifi_channel(void)
{
    return WiFi.channel();
}

extern "C" int bl_impl_wifi_ssid(char *buf, size_t size)
{
    if (WiFi.status() != WL_CONNECTED) {
        if (buf && size) {
            buf[0] = '\0';
        }
        return 0;
    }
    return copy_text(WiFi.SSID(), buf, size);
}

extern "C" int bl_impl_wifi_config(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns)
{
    // arduino-pico's order: address, DNS, gateway, subnet
    WiFi.config(IPAddress(ip), IPAddress(dns), IPAddress(gateway), IPAddress(subnet));
    return BL_OK;
}

extern "C" int bl_impl_wifi_set_hostname(const char *name)
{
    if (!name || !*name) {
        return BL_ERR_ARG;
    }
    WiFi.setHostname(name);
    return BL_OK;
}

extern "C" int bl_impl_net_resolve(const char *host, uint32_t *ip)
{
    if (!host || !ip) {
        return BL_ERR_ARG;
    }
    IPAddress addr;
    if (WiFi.hostByName(host, addr) != 1) {
        return BL_ERR_FAIL;
    }
    *ip = (uint32_t)addr;
    return BL_OK;
}

/* ---- HTTP -------------------------------------------------------------------- */
/* The response body into a fixed buffer (http_get) or one that grows (http_request). */
class BodySink : public Print {
public:
    char *buf = nullptr;
    size_t cap = 0, len = 0;
    bool grow = false, oom = false;

    size_t write(uint8_t c) override
    {
        return write(&c, 1);
    }
    size_t write(const uint8_t *data, size_t n) override
    {
        if (grow && len + n + 1 > cap) {
            size_t want = cap ? cap * 2 : 1024;
            while (want < len + n + 1) {
                want *= 2;
            }
            // Through Baseline's malloc: a big body goes to PSRAM on boards that have it
            // (realloc would keep it in internal RAM).
            char *bigger = (char *)bl_impl_malloc(want);
            if (!bigger) {
                oom = true;
                return 0;
            }
            if (buf) {
                memcpy(bigger, buf, len + 1);
                free(buf);
            }
            buf = bigger;
            cap = want;
        }
        size_t room = cap > len + 1 ? cap - 1 - len : 0;
        size_t k = n < room ? n : room;
        memcpy(buf + len, data, k);
        len += k;
        buf[len] = '\0';
        return n;       // a full fixed buffer drops the rest, as on the ESP32
    }
};

/* One request: method, headers ("Name: value\r\n" lines), body. The response
 * body goes to sink. Returns the HTTP status, or a BL_ERR_* code. */
static int http_do(const char *method, const char *url, const char *headers, const uint8_t *body, size_t body_len,
                   Print &sink)
{
    if (WiFi.status() != WL_CONNECTED) {
        return BL_ERR_STATE;
    }
    bool https = strncasecmp(url, "https:", 6) == 0;
    if (!https && strncasecmp(url, "http:", 5) != 0) {
        return BL_ERR_ARG;
    }
    int result = BL_ERR_FAIL;
    on_big_stack([&] {
        BlWdogLong wdt;     // up to the 10 s timeout, more with redirects
        WiFiClient *client = https ? new WiFiClientSecure() : new WiFiClient();
        if (https) {
            use_ca_bundle(static_cast<WiFiClientSecure *>(client));
        }
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        if (!http.begin(*client, url)) {
            delete client;
            result = BL_ERR_ARG;
            return;
        }
        if (headers && *headers) {
            String all(headers);
            int start = 0;
            while (start < (int)all.length()) {
                int eol = all.indexOf('\n', start);
                if (eol < 0) {
                    eol = all.length();
                }
                String line = all.substring(start, eol);
                line.trim();
                int colon = line.indexOf(':');
                if (colon > 0) {
                    String value = line.substring(colon + 1);
                    value.trim();
                    http.addHeader(line.substring(0, colon), value);
                }
                start = eol + 1;
            }
        }
        int code = http.sendRequest(method, body, body_len);
        if (code > 0) {
            http.writeToPrint(&sink);
            result = code;
        } else {
            result = (code == HTTPC_ERROR_READ_TIMEOUT) ? BL_ERR_TIMEOUT : BL_ERR_FAIL;
        }
        http.end();
        delete client;
    });
    return result;
}

/* A GET whose body streams into out (over-the-air updates: bl_rp2_update.cpp).
 * Returns the HTTP status, or a BL_ERR_* code. */
int bl_http_get_to(const char *url, Print &out)
{
    if (!url) {
        return BL_ERR_ARG;
    }
    return http_do("GET", url, NULL, NULL, 0, out);
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
    BodySink sink;
    sink.buf = buf;
    sink.cap = size;
    int code = http_do("GET", url, NULL, NULL, 0, sink);
    if (code < 0) {
        return code;
    }
    if (status) {
        *status = code;
    }
    return (int)sink.len;
}

extern "C" int bl_impl_http_request(const char *method, const char *url, const char *headers,
                                    const uint8_t *body, size_t body_len, char **response, size_t *response_len)
{
    if (response) {
        *response = NULL;
    }
    if (response_len) {
        *response_len = 0;
    }
    if (!url || !method) {
        return BL_ERR_ARG;
    }
    BodySink sink;
    sink.grow = true;
    int code = http_do(method, url, headers, body, body_len, sink);
    if (code < 0 || sink.oom) {
        free(sink.buf);
        return sink.oom ? BL_ERR_NO_MEM : code;
    }
    if (!sink.buf) {
        sink.buf = (char *)calloc(1, 1);    // empty body: still hand back ""
    }
    if (response) {
        *response = sink.buf;
    } else {
        free(sink.buf);
    }
    if (response_len) {
        *response_len = sink.len;
    }
    return code;
}

/* ---- TCP clients ------------------------------------------------------------- */
struct ClientSlot {
    WiFiClient *client;
    bool secure;
    int refs;
};
static ClientSlot s_clients[MAX_CLIENTS];

static WiFiClient *client_of(int h)
{
    return (h >= 0 && h < MAX_CLIENTS) ? s_clients[h].client : NULL;
}

static bool secure_of(int h)
{
    return client_of(h) && s_clients[h].secure;
}

static int client_slot(WiFiClient *c, bool secure)
{
    for (int h = 0; h < MAX_CLIENTS; h++) {
        if (!s_clients[h].client) {
            s_clients[h] = { c, secure, 1 };
            return h;
        }
    }
    delete c;
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_tcp_client_new(int secure)
{
    if (secure) {
        WiFiClientSecure *c = new WiFiClientSecure();
        use_ca_bundle(c);
        return client_slot(c, true);
    }
    return client_slot(new WiFiClient(), false);
}

extern "C" void bl_impl_tcp_client_retain(int h)
{
    if (client_of(h)) {
        s_clients[h].refs++;
    }
}

extern "C" void bl_impl_tcp_client_release(int h)
{
    WiFiClient *c = client_of(h);
    if (c && --s_clients[h].refs <= 0) {
        c->stop();
        delete c;
        s_clients[h] = {};
    }
}

extern "C" int bl_impl_tcp_client_tls_config(int h, int insecure, const char *ca_pem)
{
    if (!secure_of(h)) {
        return BL_ERR_ARG;
    }
    WiFiClientSecure *s = static_cast<WiFiClientSecure *>(s_clients[h].client);
    if (insecure) {
        s->setInsecure();
    } else if (ca_pem) {
        // Kept for the client's lifetime: BearSSL reads it during the handshake.
        static BearSSL::X509List *lists[MAX_CLIENTS];
        delete lists[h];
        lists[h] = new BearSSL::X509List(ca_pem);
        s->setTrustAnchors(lists[h]);
    } else {
        use_ca_bundle(s);
    }
    return BL_OK;
}

extern "C" int bl_impl_tcp_client_connect(int h, const char *host, uint16_t port, int32_t timeout_ms)
{
    WiFiClient *c = client_of(h);
    if (!c || !host) {
        return 0;
    }
    int r = 0;
    on_big_stack([&] {
        BlWdogLong wdt;
        if (timeout_ms > 0) {
            c->setTimeout(timeout_ms);
        }
        if (secure_of(h) && clock_set()) {
            static_cast<WiFiClientSecure *>(c)->setX509Time(time(NULL));
        }
        r = c->connect(host, port);
    });
    return r ? 1 : 0;
}

extern "C" int bl_impl_tcp_client_write(int h, const uint8_t *data, size_t len)
{
    WiFiClient *c = client_of(h);
    if (!c || !data) {
        return 0;
    }
    if (!secure_of(h)) {
        return (int)c->write(data, len);
    }
    int n = 0;
    on_big_stack([&] { n = (int)c->write(data, len); });
    return n;
}

extern "C" int bl_impl_tcp_client_available(int h)
{
    WiFiClient *c = client_of(h);
    if (!c) {
        return 0;
    }
    if (!secure_of(h)) {
        return c->available();
    }
    int n = 0;
    on_big_stack([&] { n = c->available(); });
    return n;
}

extern "C" int bl_impl_tcp_client_read(int h, uint8_t *buf, size_t size)
{
    WiFiClient *c = client_of(h);
    if (!c || !buf) {
        return -1;
    }
    int n = -1;
    auto rd = [&] { n = c->available() ? c->read(buf, size) : -1; };
    if (secure_of(h)) {
        on_big_stack(rd);
    } else {
        rd();
    }
    return n;
}

extern "C" int bl_impl_tcp_client_peek(int h)
{
    WiFiClient *c = client_of(h);
    return c ? c->peek() : -1;
}

extern "C" int bl_impl_tcp_client_connected(int h)
{
    WiFiClient *c = client_of(h);
    return (c && c->connected()) ? 1 : 0;
}

extern "C" void bl_impl_tcp_client_stop(int h)
{
    WiFiClient *c = client_of(h);
    if (c) {
        if (secure_of(h)) {
            on_big_stack([&] { c->stop(); });   // sends a TLS close_notify
        } else {
            c->stop();
        }
    }
}

extern "C" int bl_impl_tcp_client_remote(int h, uint32_t *ip, uint16_t *port)
{
    WiFiClient *c = client_of(h);
    if (!c) {
        return BL_ERR_ARG;
    }
    if (ip) {
        *ip = (uint32_t)c->remoteIP();
    }
    if (port) {
        *port = c->remotePort();
    }
    return BL_OK;
}

/* ---- TCP servers ------------------------------------------------------------- */
static WiFiServer *s_servers[MAX_SERVERS];

extern "C" int bl_impl_tcp_server_new(uint16_t port, int max_clients)
{
    for (int h = 0; h < MAX_SERVERS; h++) {
        if (!s_servers[h]) {
            s_servers[h] = new WiFiServer(port);
            s_servers[h]->begin(port, (uint8_t)(max_clients > 0 ? max_clients : 4));
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_tcp_server_accept(int h)
{
    if (h < 0 || h >= MAX_SERVERS || !s_servers[h]) {
        return -1;
    }
    WiFiClient c = s_servers[h]->accept();
    if (!c) {
        return -1;
    }
    return client_slot(new WiFiClient(c), false);
}

extern "C" void bl_impl_tcp_server_close(int h)
{
    if (h >= 0 && h < MAX_SERVERS && s_servers[h]) {
        s_servers[h]->close();
        delete s_servers[h];
        s_servers[h] = NULL;
    }
}

/* ---- UDP ----------------------------------------------------------------------- */
static WiFiUDP *s_udp[MAX_UDP];

static WiFiUDP *udp_of(int h)
{
    return (h >= 0 && h < MAX_UDP) ? s_udp[h] : NULL;
}

extern "C" int bl_impl_udp_new(uint16_t port)
{
    for (int h = 0; h < MAX_UDP; h++) {
        if (!s_udp[h]) {
            s_udp[h] = new WiFiUDP();
            if (!s_udp[h]->begin(port)) {
                delete s_udp[h];
                s_udp[h] = NULL;
                return BL_ERR_FAIL;
            }
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_udp_close(int h)
{
    WiFiUDP *u = udp_of(h);
    if (u) {
        u->stop();
        delete u;
        s_udp[h] = NULL;
    }
}

extern "C" int bl_impl_udp_begin_packet(int h, const char *host, uint16_t port)
{
    WiFiUDP *u = udp_of(h);
    return (u && host) ? u->beginPacket(host, port) : 0;
}

extern "C" int bl_impl_udp_write(int h, const uint8_t *data, size_t len)
{
    WiFiUDP *u = udp_of(h);
    return (u && data) ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_udp_end_packet(int h)
{
    WiFiUDP *u = udp_of(h);
    return u ? u->endPacket() : 0;
}

extern "C" int bl_impl_udp_parse_packet(int h)
{
    WiFiUDP *u = udp_of(h);
    return u ? u->parsePacket() : 0;
}

extern "C" int bl_impl_udp_read(int h, uint8_t *buf, size_t size)
{
    WiFiUDP *u = udp_of(h);
    return (u && buf) ? u->read(buf, size) : -1;
}

extern "C" int bl_impl_udp_remote(int h, uint32_t *ip, uint16_t *port)
{
    WiFiUDP *u = udp_of(h);
    if (!u) {
        return BL_ERR_ARG;
    }
    if (ip) {
        *ip = (uint32_t)u->remoteIP();
    }
    if (port) {
        *port = u->remotePort();
    }
    return BL_OK;
}

/* ---- WebServer ------------------------------------------------------------------ */
static WebServer *s_web[MAX_WEB];

static WebServer *web_of(int h)
{
    return (h >= 0 && h < MAX_WEB) ? s_web[h] : NULL;
}

extern "C" int bl_impl_web_new(uint16_t port)
{
    for (int h = 0; h < MAX_WEB; h++) {
        if (!s_web[h]) {
            s_web[h] = new WebServer(port);
            s_web[h]->begin();
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_web_close(int h)
{
    WebServer *w = web_of(h);
    if (w) {
        w->stop();
        delete w;
        s_web[h] = NULL;
    }
}

extern "C" int bl_impl_web_on(int h, const char *uri, int method, bl_callback_t fn, void *arg)
{
    WebServer *w = web_of(h);
    if (!w || !uri || !fn) {
        return BL_ERR_ARG;
    }
    // Runs inside web_handle_client(), which the sketch calls itself.
    w->on(uri, (HTTPMethod)method, [fn, arg]() { fn(arg); });
    return BL_OK;
}

extern "C" int bl_impl_web_on_not_found(int h, bl_callback_t fn, void *arg)
{
    WebServer *w = web_of(h);
    if (!w || !fn) {
        return BL_ERR_ARG;
    }
    w->onNotFound([fn, arg]() { fn(arg); });
    return BL_OK;
}

extern "C" void bl_impl_web_handle_client(int h)
{
    WebServer *w = web_of(h);
    if (w) {
        BlWdogLong wdt;     // a slow client can hold this for several seconds
        w->handleClient();
    }
}

extern "C" void bl_impl_web_send(int h, int code, const char *content_type, const uint8_t *content, size_t len)
{
    WebServer *w = web_of(h);
    if (w) {
        w->send(code, content_type ? content_type : "text/plain",
                (const char *)(content ? content : (const uint8_t *)""), len);
    }
}

extern "C" void bl_impl_web_send_header(int h, const char *name, const char *value, int first)
{
    WebServer *w = web_of(h);
    if (w && name && value) {
        w->sendHeader(name, value, first != 0);
    }
}

extern "C" int bl_impl_web_request(int h, int what, int index, const char *name, char *buf, size_t size)
{
    WebServer *w = web_of(h);
    if (!w) {
        return -1;
    }
    switch (what) {
    case BL_WEB_URI:         return copy_text(w->uri(), buf, size);
    case BL_WEB_METHOD:      return (int)w->method();
    case BL_WEB_ARG:         return (name && w->hasArg(name)) ? copy_text(w->arg(name), buf, size) : -1;
    case BL_WEB_ARG_AT:      return index < w->args() ? copy_text(w->arg(index), buf, size) : -1;
    case BL_WEB_ARG_NAME_AT: return index < w->args() ? copy_text(w->argName(index), buf, size) : -1;
    case BL_WEB_ARG_COUNT:   return w->args();
    case BL_WEB_HAS_ARG:     return (name && w->hasArg(name)) ? 1 : 0;
    case BL_WEB_HEADER:      return (name && w->hasHeader(name)) ? copy_text(w->header(name), buf, size) : -1;
    case BL_WEB_CLIENT_IP:   return (int)(uint32_t)w->client().remoteIP();
    default:                 return -1;
    }
}

extern "C" void bl_impl_web_collect_headers(int h, const char **names, size_t count)
{
    WebServer *w = web_of(h);
    if (w && names) {
        w->collectHeaders(names, count);
    }
}

/* ---- DNS server (captive portals) ------------------------------------------------ */
static DNSServer *s_dns;
static bool s_dns_on;

extern "C" int bl_impl_dns_start(uint16_t port, const char *domain, uint32_t ip)
{
    if (!s_dns) {
        s_dns = new DNSServer();
    }
    s_dns->stop();
    s_dns_on = s_dns->start(port ? port : 53, String(domain && *domain ? domain : "*"), IPAddress(ip));
    return s_dns_on ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_dns_stop(void)
{
    if (s_dns) {
        s_dns->stop();
    }
    s_dns_on = false;
}

extern "C" void bl_impl_dns_error_reply(int code)
{
    if (!s_dns) {
        s_dns = new DNSServer();
    }
    s_dns->setErrorReplyCode((DNSReplyCode)code);
}

/* ---- NTP ------------------------------------------------------------------------- *
 * The servers are kept; NTP starts once the station has an address (the names
 * resolve then), and again after each reconnect. */
static String s_ntp[2];
static bool s_ntp_wanted, s_ntp_running;

static void ntp_try_start(void)
{
    if (s_ntp_wanted && !s_ntp_running && WiFi.status() == WL_CONNECTED) {
        BlWdogLong wdt;     // resolving the names
        if (s_ntp[1].length()) {
            NTP.begin(s_ntp[0].c_str(), s_ntp[1].c_str());
        } else {
            NTP.begin(s_ntp[0].c_str());
        }
        s_ntp_running = true;
    }
}

extern "C" void bl_impl_time_ntp_start(const char *server1, const char *server2, const char *server3)
{
    (void)server3;      // arduino-pico's NTP takes two servers
    s_ntp[0] = server1 && *server1 ? server1 : "pool.ntp.org";
    s_ntp[1] = server2 ? server2 : "";
    s_ntp_wanted = true;
    s_ntp_running = false;
    ntp_try_start();
}

/* For HTTPS from Baseline itself (update pulls): BearSSL checks certificate
 * dates, so the clock must be set. Starts NTP with the default servers if the
 * sketch hasn't, and waits up to wait_ms. True once the time is known. */
bool bl_ntp_ensure(uint32_t wait_ms)
{
    if (!s_ntp_wanted) {
        bl_impl_time_ntp_start(NULL, NULL, NULL);
    }
    uint32_t t0 = millis();
    while (!bl_impl_time_synced() && millis() - t0 < wait_ms) {
        bl_impl_delay_ms(100);
    }
    return bl_impl_time_synced();
}

/* ---- mDNS (LEAmDNS) ---------------------------------------------------------------- */
static bool s_mdns_on;

extern "C" int bl_impl_mdns_begin(const char *hostname)
{
    if (!hostname || !*hostname) {
        return BL_ERR_ARG;
    }
    if (s_mdns_on) {
        MDNS.end();
    }
    s_mdns_on = MDNS.begin(hostname);
    return s_mdns_on ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_mdns_end(void)
{
    if (s_mdns_on) {
        MDNS.end();
        s_mdns_on = false;
    }
}

static bool mdns_ensure(const char *hostname)
{
    return s_mdns_on || bl_impl_mdns_begin(hostname) == BL_OK;
}

extern "C" int bl_impl_mdns_add_service(const char *service, const char *proto, uint16_t port)
{
    if (!s_mdns_on || !service || !proto) {
        return BL_ERR_STATE;
    }
    return MDNS.addService(NULL, service, proto, port) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_mdns_add_txt(const char *service, const char *proto, const char *key, const char *value)
{
    if (!s_mdns_on || !service || !proto || !key || !value) {
        return BL_ERR_STATE;
    }
    return MDNS.addServiceTxt(service, proto, key, value) ? BL_OK : BL_ERR_FAIL;
}

/* LEAmDNS has no single host lookup: NAME.local can't be resolved. */
extern "C" uint32_t bl_impl_mdns_query_host(const char *host, uint32_t timeout_ms)
{
    (void)host;
    (void)timeout_ms;
    return 0;
}

extern "C" int bl_impl_mdns_query_service(const char *service, const char *proto)
{
    if (!service || !proto) {
        return BL_ERR_ARG;
    }
    if (!mdns_ensure("baseline")) {
        return BL_ERR_FAIL;
    }
    BlWdogLong wdt;
    return (int)MDNS.queryService(service, proto);
}

extern "C" int bl_impl_mdns_result(int index, int what, char *buf, size_t size)
{
    if (index < 0) {
        return -1;
    }
    switch (what) {
    case BL_MDNS_HOSTNAME: return copy_text(MDNS.hostname((uint32_t)index), buf, size);
    case BL_MDNS_IP:       return (int)(uint32_t)MDNS.IP((uint32_t)index);
    case BL_MDNS_PORT:     return MDNS.port((uint32_t)index);
    default:               return -1;
    }
}

/* ---- WiFi uploads (ArduinoOTA.begin) ---------------------------------------------
 * A TCP port (default 3232) that runs the same "@BSK ..." lines as the USB
 * console, served from bl_poll(): the sketch has to keep calling loop() or
 * delay(), as for ArduinoOTA.handle(). With a password, a connection sends
 * "@BSK AUTH <password>" first. */
#define OTA_LINE_MAX    1600
#define OTA_IDLE_MS     30000
static WiFiServer *s_ota;
static WiFiClient s_ota_client;
static String s_ota_password;
static char *s_ota_line;
static size_t s_ota_len;
static bool s_ota_authed;
static uint32_t s_ota_last;

static void ota_send(const char *text, size_t len, void *ctx)
{
    ((WiFiClient *)ctx)->write((const uint8_t *)text, len);
}

extern "C" int bl_impl_ota_begin(const char *hostname, const char *password, uint16_t port)
{
    if (s_ota) {
        return BL_OK;
    }
    if (!s_ota_line && !(s_ota_line = (char *)malloc(OTA_LINE_MAX))) {
        return BL_ERR_NO_MEM;
    }
    s_ota_password = password ? password : "";
    if (!port) {
        port = 3232;
    }
    if (hostname && *hostname && mdns_ensure(hostname)) {
        MDNS.addService(NULL, "flick", "tcp", port);
    }
    s_ota = new WiFiServer(port);
    s_ota->begin();
    return BL_OK;
}

extern "C" void bl_impl_ota_end(void)
{
    if (s_ota) {
        s_ota_client.stop();
        s_ota->close();
        delete s_ota;
        s_ota = NULL;
    }
}

static void ota_poll(void)
{
    if (!s_ota) {
        return;
    }
    if (!s_ota_client || !s_ota_client.connected()) {
        WiFiClient c = s_ota->accept();
        if (!c) {
            return;
        }
        s_ota_client = c;
        s_ota_authed = s_ota_password.isEmpty();
        s_ota_len = 0;
        s_ota_last = millis();
    }
    if (millis() - s_ota_last > OTA_IDLE_MS) {
        s_ota_client.stop();
        return;
    }
    uint8_t buf[256];
    // A burst of lines: flick keeps several in flight during an upload.
    for (int rounds = 0; rounds < 32 && s_ota_client.available() > 0; rounds++) {
        int n = s_ota_client.read(buf, sizeof(buf));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            s_ota_last = millis();
            if (c != '\n') {
                if (c != '\r' && s_ota_len < OTA_LINE_MAX - 1) {
                    s_ota_line[s_ota_len++] = c;
                }
                continue;
            }
            s_ota_line[s_ota_len] = '\0';
            s_ota_len = 0;
            if (strncmp(s_ota_line, "@BSK AUTH ", 10) == 0) {
                s_ota_authed = s_ota_password == (s_ota_line + 10);
                s_ota_client.print(s_ota_authed ? "@BSK OK authorized\n" : "@BSK ERR wrong password\n");
            } else if (strncmp(s_ota_line, "@BSK ", 5) != 0) {
                // not for the loader
            } else if (!s_ota_authed) {
                s_ota_client.print("@BSK ERR password required\n");
            } else {
                bl_loader_line(s_ota_line, ota_send, &s_ota_client);
            }
        }
    }
}

/* ---- From bl_poll() -------------------------------------------------------------- */
static void watch_state(void)
{
    static bool was_up;
    static int was_stations;
    bool up = WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0;
    if (up != was_up) {
        if (up) {
            event(EV_STA_CONNECTED);
            event(EV_STA_GOT_IP);
            ntp_try_start();
        } else {
            event(EV_STA_DISCONNECTED);
            event(EV_STA_LOST_IP);
            s_ntp_running = false;
        }
        was_up = up;
    }
    if (s_ap_on) {
        int n = WiFi.softAPgetStationNum();
        if (n != was_stations) {
            event(n > was_stations ? EV_AP_STACONNECTED : EV_AP_STADISCONNECTED);
            was_stations = n;
        }
    } else {
        was_stations = 0;
    }
}

extern "C" void bl_net_poll(void)
{
    static uint32_t last_state;
    if (millis() - last_state >= 100) {
        last_state = millis();
        watch_state();
    }
    if (s_mdns_on) {
        MDNS.update();
    }
    if (s_dns_on) {
        s_dns->processNextRequest();
    }
    bl_mqtt_poll();
    ota_poll();
}

#endif /* BL_HAS_WIFI */
