/* API table: TCP client/server, UDP, and WebServer (ABI 1.3).
 * Baseline owns the Arduino objects; sketches refer to them by small integer
 * handles, which the SDK wraps in WiFiClient, WiFiServer, WiFiUDP, WebServer. */
#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <WebServer.h>
#include "gen/bl_impl.h"
#include "sketch_loader.h"

#define MAX_CLIENTS     12
#define MAX_SERVERS     4
#define MAX_UDP         4
#define MAX_WEB         2

/* Built-in CA bundle (the same one esp_http_client uses), for TLS clients. */
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static_assert(BL_HTTP_DELETE == HTTP_DELETE && BL_HTTP_GET == HTTP_GET && BL_HTTP_HEAD == HTTP_HEAD &&
              BL_HTTP_POST == HTTP_POST && BL_HTTP_PUT == HTTP_PUT && BL_HTTP_OPTIONS == HTTP_OPTIONS &&
              BL_HTTP_PATCH == HTTP_PATCH && BL_HTTP_ANY == HTTP_ANY, "BL_HTTP_* must equal HTTPMethod");

/* ---- TCP clients ---- */

struct ClientSlot {
    NetworkClient *client;
    bool secure;
    int refs;
};
static ClientSlot s_clients[MAX_CLIENTS];

static NetworkClient *client_of(int h)
{
    return (h >= 0 && h < MAX_CLIENTS) ? s_clients[h].client : NULL;
}

static int client_slot(NetworkClient *c, bool secure)
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

static void use_ca_bundle(NetworkClientSecure *c)
{
    c->setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
}

extern "C" int bl_impl_tcp_client_new(int secure)
{
    if (secure) {
        NetworkClientSecure *c = new NetworkClientSecure();
        use_ca_bundle(c);
        return client_slot(c, true);
    }
    return client_slot(new NetworkClient(), false);
}

extern "C" void bl_impl_tcp_client_retain(int h)
{
    if (client_of(h)) {
        s_clients[h].refs++;
    }
}

extern "C" void bl_impl_tcp_client_release(int h)
{
    NetworkClient *c = client_of(h);
    if (c && --s_clients[h].refs <= 0) {
        c->stop();
        delete c;
        s_clients[h] = {};
    }
}

extern "C" int bl_impl_tcp_client_tls_config(int h, int insecure, const char *ca_pem)
{
    NetworkClient *c = client_of(h);
    if (!c || !s_clients[h].secure) {
        return BL_ERR_ARG;
    }
    NetworkClientSecure *s = static_cast<NetworkClientSecure *>(c);
    if (insecure) {
        s->setInsecure();
    } else if (ca_pem) {
        s->setCACert(ca_pem);
    } else {
        use_ca_bundle(s);
    }
    return BL_OK;
}

extern "C" int bl_impl_tcp_client_connect(int h, const char *host, uint16_t port, int32_t timeout_ms)
{
    NetworkClient *c = client_of(h);
    if (!c || !host) {
        return 0;
    }
    WdtPause wdt;
    int r = timeout_ms > 0 ? c->connect(host, port, timeout_ms) : c->connect(host, port);
    return r ? 1 : 0;
}

extern "C" int bl_impl_tcp_client_write(int h, const uint8_t *data, size_t len)
{
    NetworkClient *c = client_of(h);
    return (c && data) ? (int)c->write(data, len) : 0;
}

extern "C" int bl_impl_tcp_client_available(int h)
{
    NetworkClient *c = client_of(h);
    return c ? c->available() : 0;
}

extern "C" int bl_impl_tcp_client_read(int h, uint8_t *buf, size_t size)
{
    NetworkClient *c = client_of(h);
    if (!c || !buf || !c->available()) {
        return -1;
    }
    return c->read(buf, size);
}

extern "C" int bl_impl_tcp_client_peek(int h)
{
    NetworkClient *c = client_of(h);
    return c ? c->peek() : -1;
}

extern "C" int bl_impl_tcp_client_connected(int h)
{
    NetworkClient *c = client_of(h);
    return (c && c->connected()) ? 1 : 0;
}

extern "C" void bl_impl_tcp_client_stop(int h)
{
    NetworkClient *c = client_of(h);
    if (c) {
        c->stop();
    }
}

extern "C" int bl_impl_tcp_client_remote(int h, uint32_t *ip, uint16_t *port)
{
    NetworkClient *c = client_of(h);
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

/* ---- TCP servers ---- */

static NetworkServer *s_servers[MAX_SERVERS];

extern "C" int bl_impl_tcp_server_new(uint16_t port, int max_clients)
{
    for (int h = 0; h < MAX_SERVERS; h++) {
        if (!s_servers[h]) {
            s_servers[h] = new NetworkServer(port, max_clients > 0 ? max_clients : 4);
            s_servers[h]->begin();
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
    NetworkClient c = s_servers[h]->accept();
    if (!c) {
        return -1;
    }
    return client_slot(new NetworkClient(c), false);
}

extern "C" void bl_impl_tcp_server_close(int h)
{
    if (h >= 0 && h < MAX_SERVERS && s_servers[h]) {
        s_servers[h]->end();
        delete s_servers[h];
        s_servers[h] = NULL;
    }
}

/* ---- UDP ---- */

static NetworkUDP *s_udp[MAX_UDP];

static NetworkUDP *udp_of(int h)
{
    return (h >= 0 && h < MAX_UDP) ? s_udp[h] : NULL;
}

extern "C" int bl_impl_udp_new(uint16_t port)
{
    for (int h = 0; h < MAX_UDP; h++) {
        if (!s_udp[h]) {
            s_udp[h] = new NetworkUDP();
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
    NetworkUDP *u = udp_of(h);
    if (u) {
        u->stop();
        delete u;
        s_udp[h] = NULL;
    }
}

extern "C" int bl_impl_udp_begin_packet(int h, const char *host, uint16_t port)
{
    NetworkUDP *u = udp_of(h);
    return (u && host) ? u->beginPacket(host, port) : 0;
}

extern "C" int bl_impl_udp_write(int h, const uint8_t *data, size_t len)
{
    NetworkUDP *u = udp_of(h);
    return (u && data) ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_udp_end_packet(int h)
{
    NetworkUDP *u = udp_of(h);
    return u ? u->endPacket() : 0;
}

extern "C" int bl_impl_udp_parse_packet(int h)
{
    NetworkUDP *u = udp_of(h);
    return u ? u->parsePacket() : 0;
}

extern "C" int bl_impl_udp_read(int h, uint8_t *buf, size_t size)
{
    NetworkUDP *u = udp_of(h);
    return (u && buf) ? u->read(buf, size) : -1;
}

extern "C" int bl_impl_udp_remote(int h, uint32_t *ip, uint16_t *port)
{
    NetworkUDP *u = udp_of(h);
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

/* ---- WebServer ---- */

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
    // Runs inside web_handle_client(), which the sketch calls on its own task.
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
        WdtPause wdt;   // a slow client can hold this for several seconds
        w->handleClient();
    }
}

extern "C" void bl_impl_web_send(int h, int code, const char *content_type, const uint8_t *content, size_t len)
{
    WebServer *w = web_of(h);
    if (w) {
        w->send_P(code, content_type ? content_type : "text/plain", (const char *)(content ? content : (const uint8_t *)""), len);
    }
}

extern "C" void bl_impl_web_send_header(int h, const char *name, const char *value, int first)
{
    WebServer *w = web_of(h);
    if (w && name && value) {
        w->sendHeader(name, value, first != 0);
    }
}

static int copy_text(const String &s, char *buf, size_t size)
{
    if (buf && size) {
        strlcpy(buf, s.c_str(), size);
    }
    return (int)s.length();
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
