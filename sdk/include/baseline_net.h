/*
 * baseline_net.h - Arduino networking classes for sketches: WiFi (station,
 * access point, scan), WiFiClient, WiFiClientSecure, WiFiServer, WiFiUDP,
 * HTTPClient, WebServer, MDNS, ArduinoOTA. Each object holds a handle to the real Arduino
 * object inside Baseline. Included by baseline_arduino.h.
 */
#ifndef BASELINE_NET_H
#define BASELINE_NET_H

#ifdef __cplusplus

/* ---------------- WiFi ---------------- */

typedef int wl_status_t;
#define WL_IDLE_STATUS     BL_WIFI_IDLE
#define WL_NO_SSID_AVAIL   BL_WIFI_NO_SSID
#define WL_SCAN_COMPLETED  BL_WIFI_SCAN_COMPLETED
#define WL_CONNECTED       BL_WIFI_CONNECTED
#define WL_CONNECT_FAILED  BL_WIFI_CONNECT_FAILED
#define WL_CONNECTION_LOST BL_WIFI_CONNECTION_LOST
#define WL_DISCONNECTED    BL_WIFI_DISCONNECTED
#define WL_STOPPED         BL_WIFI_STOPPED
#define WL_NO_SHIELD       BL_WIFI_NO_SHIELD

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
#define WIFI_MODE_NULL  WIFI_OFF
#define WIFI_MODE_STA   WIFI_STA
#define WIFI_MODE_AP    WIFI_AP
#define WIFI_MODE_APSTA WIFI_AP_STA

typedef enum {
    WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED  (-2)

/* ---- WiFi / network events (WiFi.onEvent) ----
 * Callbacks run on the sketch task (inside delay()/yield() or between loop()
 * calls), like interrupt handlers. The numbers are Arduino-ESP32 3.3's. */
typedef enum {
    ARDUINO_EVENT_NONE = 0,
    ARDUINO_EVENT_ETH_START, ARDUINO_EVENT_ETH_STOP, ARDUINO_EVENT_ETH_CONNECTED, ARDUINO_EVENT_ETH_DISCONNECTED,
    ARDUINO_EVENT_ETH_GOT_IP, ARDUINO_EVENT_ETH_LOST_IP, ARDUINO_EVENT_ETH_GOT_IP6,
    ARDUINO_EVENT_WIFI_OFF = 100, ARDUINO_EVENT_WIFI_READY, ARDUINO_EVENT_WIFI_SCAN_DONE, ARDUINO_EVENT_WIFI_FTM_REPORT,
    ARDUINO_EVENT_WIFI_STA_START = 110, ARDUINO_EVENT_WIFI_STA_STOP, ARDUINO_EVENT_WIFI_STA_CONNECTED,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE, ARDUINO_EVENT_WIFI_STA_GOT_IP,
    ARDUINO_EVENT_WIFI_STA_GOT_IP6, ARDUINO_EVENT_WIFI_STA_LOST_IP,
    ARDUINO_EVENT_WIFI_AP_START = 130, ARDUINO_EVENT_WIFI_AP_STOP, ARDUINO_EVENT_WIFI_AP_STACONNECTED,
    ARDUINO_EVENT_WIFI_AP_STADISCONNECTED, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED, ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED,
    ARDUINO_EVENT_WIFI_AP_GOT_IP6,
    ARDUINO_EVENT_MAX = 207,
} arduino_event_id_t;
typedef arduino_event_id_t WiFiEvent_t;
typedef arduino_event_id_t arduino_event_t;
typedef struct { int unused; } WiFiEventInfo_t;      // event details are not passed through
typedef void (*WiFiEventCb)(arduino_event_id_t event);
typedef void (*WiFiEventSysCb)(arduino_event_id_t event, WiFiEventInfo_t info);
typedef int wifi_event_id_t;

/* Register a callback for one event, or every event (ARDUINO_EVENT_MAX). Returns an id. */
wifi_event_id_t bl_wifi_on_event(WiFiEventCb cb, WiFiEventSysCb cb2, arduino_event_id_t event);
void bl_wifi_remove_event(wifi_event_id_t id);

class WiFiClass {
public:
    /* station */
    wl_status_t begin(const char *ssid, const char *password = 0)
    {
        bl_wifi_begin(ssid, password);
        return bl_wifi_status();
    }
    wl_status_t begin(const String &ssid, const String &password) { return begin(ssid.c_str(), password.c_str()); }
    wl_status_t status() { return bl_wifi_status(); }
    bool isConnected() { return bl_wifi_status() == WL_CONNECTED; }
    /* WiFi.onEvent(cb[, event]): cb(event) or cb(event, info), on the sketch task. */
    wifi_event_id_t onEvent(WiFiEventCb cb, arduino_event_id_t event = ARDUINO_EVENT_MAX) { return bl_wifi_on_event(cb, nullptr, event); }
    wifi_event_id_t onEvent(WiFiEventSysCb cb, arduino_event_id_t event = ARDUINO_EVENT_MAX) { return bl_wifi_on_event(nullptr, cb, event); }
    void removeEvent(wifi_event_id_t id) { bl_wifi_remove_event(id); }
    bool disconnect(bool wifioff = false, bool eraseap = false)
    {
        bl_wifi_disconnect();
        if (wifioff) {
            bl_wifi_set_mode(BL_WIFI_MODE_OFF);
        }
        return true;
    }
    bool reconnect() { return false; }
    bool config(IPAddress ip, IPAddress gateway, IPAddress subnet, IPAddress dns = IPAddress())
    {
        return bl_wifi_config(ip, gateway, subnet, dns) == BL_OK;
    }
    bool setHostname(const char *name) { return bl_wifi_set_hostname(name) == BL_OK; }
    bool setSleep(bool) { return true; }
    bool setAutoReconnect(bool) { return true; }

    /* mode */
    bool mode(wifi_mode_t m) { return bl_wifi_set_mode((int)m) == BL_OK; }
    wifi_mode_t getMode() { return (wifi_mode_t)bl_wifi_get_mode(); }

    /* addresses */
    IPAddress localIP() { return IPAddress(bl_wifi_ip(BL_IP_STA)); }
    IPAddress gatewayIP() { return IPAddress(bl_wifi_ip(BL_IP_GATEWAY)); }
    IPAddress subnetMask() { return IPAddress(bl_wifi_ip(BL_IP_SUBNET)); }
    IPAddress dnsIP(uint8_t = 0) { return IPAddress(bl_wifi_ip(BL_IP_DNS)); }
    String macAddress();
    uint8_t *macAddress(uint8_t *mac) { bl_wifi_mac(mac); return mac; }
    String SSID();
    int8_t RSSI() { return (int8_t)bl_wifi_rssi(); }
    int32_t channel() { return bl_wifi_channel(); }
    int hostByName(const char *host, IPAddress &result)
    {
        uint32_t ip = 0;
        if (bl_net_resolve(host, &ip) != BL_OK) {
            return 0;
        }
        result = IPAddress(ip);
        return 1;
    }

    /* access point */
    bool softAP(const char *ssid, const char *password = 0, int channel = 1, int hidden = 0, int max_connection = 4)
    {
        return bl_wifi_softap(ssid, password, channel, hidden, max_connection) == BL_OK;
    }
    bool softAP(const String &ssid, const String &password = String()) { return softAP(ssid.c_str(), password.length() ? password.c_str() : 0); }
    bool softAPdisconnect(bool wifioff = false)
    {
        bool ok = bl_wifi_softap_stop() == BL_OK;
        if (wifioff) {
            bl_wifi_set_mode(BL_WIFI_MODE_OFF);
        }
        return ok;
    }
    IPAddress softAPIP() { return IPAddress(bl_wifi_ip(BL_IP_SOFTAP)); }
    uint8_t softAPgetStationNum() { return (uint8_t)bl_wifi_softap_stations(); }

    /* scan (blocking; the async form returns the same result) */
    int16_t scanNetworks(bool async = false, bool show_hidden = false)
    {
        int n = bl_wifi_scan();
        scanCount_ = n > 0 ? n : 0;
        return n >= 0 ? (int16_t)n : WIFI_SCAN_FAILED;
    }
    int16_t scanComplete() { return (int16_t)scanCount_; }
    void scanDelete() { scanCount_ = 0; }
    String SSID(uint8_t i);
    int32_t RSSI(uint8_t i) { bl_wifi_ap_t ap; return scanResult(i, ap) ? ap.rssi : 0; }
    int32_t channel(uint8_t i) { bl_wifi_ap_t ap; return scanResult(i, ap) ? ap.channel : 0; }
    wifi_auth_mode_t encryptionType(uint8_t i) { bl_wifi_ap_t ap; return scanResult(i, ap) ? (wifi_auth_mode_t)ap.auth : WIFI_AUTH_OPEN; }
    String BSSIDstr(uint8_t i);

private:
    bool scanResult(uint8_t i, bl_wifi_ap_t &ap) { return bl_wifi_scan_result(i, &ap) == BL_OK; }
    int scanCount_ = 0;
};

extern WiFiClass WiFi;

/* ---------------- TCP client ---------------- */

/* Copies share one connection (reference counted), as in Arduino-ESP32. */
class WiFiClient : public Stream {
public:
    WiFiClient() {}
    explicit WiFiClient(int handle) : h_(handle) {}     // takes ownership of a handle
    WiFiClient(const WiFiClient &o) : Stream(o), h_(o.h_) { retain(); }
    WiFiClient &operator=(const WiFiClient &o)
    {
        if (this != &o) {
            release();
            h_ = o.h_;
            retain();
        }
        return *this;
    }
    virtual ~WiFiClient() { release(); }

    int connect(const char *host, uint16_t port, int32_t timeout_ms = 0);
    int connect(IPAddress ip, uint16_t port, int32_t timeout_ms = 0) { return connect(ip.toString().c_str(), port, timeout_ms); }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return h_ >= 0 ? (size_t)bl_tcp_client_write(h_, buf, size) : 0; }
    int available() override { return h_ >= 0 ? bl_tcp_client_available(h_) : 0; }
    int read() override;
    int read(uint8_t *buf, size_t size) { return h_ >= 0 ? bl_tcp_client_read(h_, buf, size) : -1; }
    int peek() { return h_ >= 0 ? bl_tcp_client_peek(h_) : -1; }
    void flush() {}
    void stop() { if (h_ >= 0) bl_tcp_client_stop(h_); }
    uint8_t connected() { return h_ >= 0 && bl_tcp_client_connected(h_); }
    operator bool() { return connected(); }
    bool operator==(const WiFiClient &o) const { return h_ == o.h_; }
    bool operator!=(const WiFiClient &o) const { return h_ != o.h_; }
    IPAddress remoteIP();
    uint16_t remotePort();

protected:
    friend class PubSubClient;  // reads isSecure() to choose MQTT over TLS
    virtual bool isSecure() const { return false; }
    virtual void configure() {}
    void retain() { if (h_ >= 0) bl_tcp_client_retain(h_); }
    void release()
    {
        if (h_ >= 0) {
            bl_tcp_client_release(h_);
            h_ = -1;
        }
    }
    int h_ = -1;
};
typedef WiFiClient NetworkClient;

/* TLS client. With neither setInsecure() nor setCACert(), certificates are
 * checked against Baseline's built-in CA bundle. */
class WiFiClientSecure : public WiFiClient {
public:
    void setInsecure() { insecure_ = true; ca_ = 0; }
    void setCACert(const char *rootCA) { ca_ = rootCA; insecure_ = false; }  // must stay valid
    void setCACertBundle(const uint8_t *, size_t = 0) { ca_ = 0; insecure_ = false; }

protected:
    bool isSecure() const override { return true; }
    void configure() override { bl_tcp_client_tls_config(h_, insecure_, ca_); }

private:
    friend class PubSubClient;  // passes ca_ to Baseline's MQTT client
    bool insecure_ = false;
    const char *ca_ = 0;
};
typedef WiFiClientSecure NetworkClientSecure;

/* ---------------- TCP server ---------------- */

class WiFiServer {
public:
    WiFiServer(uint16_t port = 80, uint8_t max_clients = 4) : port_(port), max_(max_clients) {}
    void begin(uint16_t port = 0)
    {
        if (port) {
            port_ = port;
        }
        if (h_ < 0) {
            h_ = bl_tcp_server_new(port_, max_);
        }
    }
    bool hasClient()
    {
        if (pending_ < 0 && h_ >= 0) {
            pending_ = bl_tcp_server_accept(h_);
        }
        return pending_ >= 0;
    }
    WiFiClient accept()
    {
        int c = pending_;
        pending_ = -1;
        if (c < 0 && h_ >= 0) {
            c = bl_tcp_server_accept(h_);
        }
        return c >= 0 ? WiFiClient(c) : WiFiClient();
    }
    WiFiClient available() { return accept(); }
    void end()
    {
        if (h_ >= 0) {
            bl_tcp_server_close(h_);
            h_ = -1;
        }
    }
    void close() { end(); }
    void stop() { end(); }
    operator bool() { return h_ >= 0; }

private:
    uint16_t port_;
    uint8_t max_;
    int h_ = -1;
    int pending_ = -1;
};
typedef WiFiServer NetworkServer;

/* ---------------- UDP ---------------- */

class WiFiUDP : public Stream {
public:
    uint8_t begin(uint16_t port)
    {
        stop();
        h_ = bl_udp_new(port);
        return h_ >= 0;
    }
    uint8_t begin(IPAddress, uint16_t port) { return begin(port); }
    void stop()
    {
        if (h_ >= 0) {
            bl_udp_close(h_);
            h_ = -1;
        }
        remaining_ = 0;
    }
    int beginPacket(const char *host, uint16_t port)
    {
        if (h_ < 0) {
            h_ = bl_udp_new(0);
        }
        return h_ >= 0 ? bl_udp_begin_packet(h_, host, port) : 0;
    }
    int beginPacket(IPAddress ip, uint16_t port) { return beginPacket(ip.toString().c_str(), port); }
    int endPacket() { return h_ >= 0 ? bl_udp_end_packet(h_) : 0; }
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return h_ >= 0 ? (size_t)bl_udp_write(h_, buf, size) : 0; }
    int parsePacket()
    {
        int n = h_ >= 0 ? bl_udp_parse_packet(h_) : 0;
        remaining_ = n > 0 ? n : 0;
        return n;
    }
    int available() override { return remaining_; }
    int read() override;
    int read(uint8_t *buf, size_t size);
    int read(char *buf, size_t size) { return read((uint8_t *)buf, size); }
    void flush() { remaining_ = 0; }
    IPAddress remoteIP();
    uint16_t remotePort();

private:
    int h_ = -1;
    int remaining_ = 0;
};
typedef WiFiUDP NetworkUDP;

/* ---------------- HTTPClient ---------------- */

#define HTTP_CODE_OK                    200
#define HTTP_CODE_CREATED               201
#define HTTP_CODE_NO_CONTENT            204
#define HTTP_CODE_MOVED_PERMANENTLY     301
#define HTTP_CODE_FOUND                 302
#define HTTP_CODE_BAD_REQUEST           400
#define HTTP_CODE_UNAUTHORIZED          401
#define HTTP_CODE_FORBIDDEN             403
#define HTTP_CODE_NOT_FOUND             404
#define HTTP_CODE_INTERNAL_SERVER_ERROR 500

#define HTTPC_ERROR_CONNECTION_REFUSED  (-1)
#define HTTPC_ERROR_NOT_CONNECTED       (-4)
#define HTTPC_ERROR_TOO_LESS_RAM        (-8)
#define HTTPC_ERROR_READ_TIMEOUT        (-11)

typedef enum { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS } followRedirects_t;

/* Requests are made by Baseline's HTTP client (TLS checked with its CA bundle);
 * the WiFiClient passed to begin() is accepted for compatibility. */
class HTTPClient {
public:
    bool begin(const String &url) { url_ = url; headers_ = ""; body_ = ""; return url_.length() > 0; }
    bool begin(const char *url) { return begin(String(url)); }
    bool begin(WiFiClient &, const String &url) { return begin(url); }
    bool begin(WiFiClient &, const char *url) { return begin(String(url)); }
    void end() { body_ = ""; }
    void addHeader(const String &name, const String &value) { headers_ += name + ": " + value + "\r\n"; }
    void setAuthorization(const char *user, const char *password);
    void setTimeout(uint16_t) {}
    void setConnectTimeout(int32_t) {}
    void setReuse(bool) {}
    void setFollowRedirects(followRedirects_t) {}

    int GET() { return sendRequest("GET"); }
    int POST(const String &payload) { return sendRequest("POST", payload); }
    int POST(const uint8_t *payload, size_t size) { return sendRequest("POST", payload, size); }
    int PUT(const String &payload) { return sendRequest("PUT", payload); }
    int PATCH(const String &payload) { return sendRequest("PATCH", payload); }
    int sendRequest(const char *method, const String &payload = String())
    {
        return sendRequest(method, (const uint8_t *)payload.c_str(), payload.length());
    }
    int sendRequest(const char *method, const uint8_t *payload, size_t size);

    String getString() { return body_; }
    int getSize() { return (int)body_.length(); }
    static String errorToString(int error);

private:
    String url_;
    String headers_;
    String body_;
};

/* ---------------- WebServer ---------------- */

typedef int HTTPMethod;
#define HTTP_DELETE  BL_HTTP_DELETE
#define HTTP_GET     BL_HTTP_GET
#define HTTP_HEAD    BL_HTTP_HEAD
#define HTTP_POST    BL_HTTP_POST
#define HTTP_PUT     BL_HTTP_PUT
#define HTTP_OPTIONS BL_HTTP_OPTIONS
#define HTTP_PATCH   BL_HTTP_PATCH
#define HTTP_ANY     BL_HTTP_ANY

/* Handlers are plain functions or capture-less lambdas: void handler(). They
 * run inside handleClient(), on the sketch task. */
class WebServer {
public:
    typedef void (*THandlerFunction)();

    WebServer(int port = 80) : port_(port) {}
    void begin() { ensure(); }
    void begin(uint16_t port) { port_ = port; ensure(); }
    void stop() { if (h_ >= 0) { bl_web_close(h_); h_ = -1; } }
    void close() { stop(); }
    void handleClient() { if (h_ >= 0) bl_web_handle_client(h_); }

    void on(const char *uri, THandlerFunction fn) { on(uri, HTTP_ANY, fn); }
    void on(const char *uri, HTTPMethod method, THandlerFunction fn);
    void on(const String &uri, THandlerFunction fn) { on(uri.c_str(), HTTP_ANY, fn); }
    void on(const String &uri, HTTPMethod method, THandlerFunction fn) { on(uri.c_str(), method, fn); }
    void onNotFound(THandlerFunction fn);

    void send(int code, const char *content_type = 0, const String &content = String())
    {
        sendRaw(code, content_type, content.c_str(), content.length());
    }
    void send(int code, const char *content_type, const char *content) { sendRaw(code, content_type, content, strlen(content)); }
    void send(int code, const String &content_type, const String &content) { send(code, content_type.c_str(), content); }
    void send_P(int code, const char *content_type, const char *content, size_t len) { sendRaw(code, content_type, content, len); }
    void send_P(int code, const char *content_type, const char *content) { send(code, content_type, content); }
    void sendHeader(const String &name, const String &value, bool first = false)
    {
        if (h_ >= 0) bl_web_send_header(h_, name.c_str(), value.c_str(), first);
    }

    String uri() { return text(BL_WEB_URI, 0, 0); }
    HTTPMethod method() { return h_ >= 0 ? bl_web_request(h_, BL_WEB_METHOD, 0, 0, 0, 0) : HTTP_GET; }
    String arg(const String &name) { return text(BL_WEB_ARG, 0, name.c_str()); }
    String arg(int i) { return text(BL_WEB_ARG_AT, i, 0); }
    String argName(int i) { return text(BL_WEB_ARG_NAME_AT, i, 0); }
    int args() { return h_ >= 0 ? bl_web_request(h_, BL_WEB_ARG_COUNT, 0, 0, 0, 0) : 0; }
    bool hasArg(const String &name) { return h_ >= 0 && bl_web_request(h_, BL_WEB_HAS_ARG, 0, name.c_str(), 0, 0) == 1; }
    String header(const String &name) { return text(BL_WEB_HEADER, 0, name.c_str()); }
    bool hasHeader(const String &name) { return h_ >= 0 && bl_web_request(h_, BL_WEB_HEADER, 0, name.c_str(), 0, 0) >= 0; }
    void collectHeaders(const char *keys[], size_t count) { ensure(); bl_web_collect_headers(h_, keys, count); }

    /* server.client().remoteIP() */
    struct ClientInfo {
        uint32_t ip;
        IPAddress remoteIP() const { return IPAddress(ip); }
    };
    ClientInfo client() { return ClientInfo{ h_ >= 0 ? (uint32_t)bl_web_request(h_, BL_WEB_CLIENT_IP, 0, 0, 0, 0) : 0 }; }

private:
    void ensure() { if (h_ < 0) h_ = bl_web_new((uint16_t)port_); }
    void sendRaw(int code, const char *type, const char *content, size_t len)
    {
        if (h_ >= 0) bl_web_send(h_, code, type, (const uint8_t *)content, len);
    }
    String text(int what, int index, const char *name);
    int port_;
    int h_ = -1;
};

/* ---------------- DNSServer (captive portals) ----------------
 * Answers every DNS lookup for domainName ("*" = all) with resolvedIP, so
 * phones joining your access point open its web page. Requests are handled
 * inside Baseline; processNextRequest() has nothing to do. */
enum class DNSReplyCode : uint16_t {
    NoError = 0, FormError = 1, ServerFailure = 2, NonExistentDomain = 3, NotImplemented = 4,
    Refused = 5, YXDomain = 6, YXRRSet = 7, NXRRSet = 8,
};

class DNSServer {
public:
    bool start(const uint16_t &port, const String &domainName, const IPAddress &resolvedIP)
    {
        return bl_dns_start(port, domainName.c_str(), (uint32_t)resolvedIP) == BL_OK;
    }
    bool start() { return start(53, String("*"), IPAddress(bl_wifi_ip(BL_IP_SOFTAP))); }
    void stop() { bl_dns_stop(); }
    void processNextRequest() {}
    void setErrorReplyCode(const DNSReplyCode &replyCode) { bl_dns_error_reply((int)replyCode); }
    void setTTL(const uint32_t &ttl) {}
};

/* ---------------- mDNS ---------------- */

/* Service and protocol names work with or without the leading '_'. */
class MDNSResponder {
public:
    bool begin(const char *hostName) { return bl_mdns_begin(hostName) == BL_OK; }
    bool begin(const String &hostName) { return begin(hostName.c_str()); }
    void end() { bl_mdns_end(); }
    bool setInstanceName(const char *) { return true; }
    bool addService(const char *service, const char *proto, uint16_t port) { return bl_mdns_add_service(service, proto, port) == BL_OK; }
    bool addService(const String &service, const String &proto, uint16_t port) { return addService(service.c_str(), proto.c_str(), port); }
    bool addServiceTxt(const char *service, const char *proto, const char *key, const char *value)
    {
        return bl_mdns_add_txt(service, proto, key, value) == BL_OK;
    }
    bool addServiceTxt(const String &service, const String &proto, const String &key, const String &value)
    {
        return addServiceTxt(service.c_str(), proto.c_str(), key.c_str(), value.c_str());
    }
    /* Resolve NAME.local; pass NAME without ".local". */
    IPAddress queryHost(const char *host, uint32_t timeout = 2000) { return IPAddress(bl_mdns_query_host(host, timeout)); }
    IPAddress queryHost(const String &host, uint32_t timeout = 2000) { return queryHost(host.c_str(), timeout); }
    /* Browse; then read results with hostname(i), address(i), port(i). */
    int queryService(const char *service, const char *proto) { return bl_mdns_query_service(service, proto); }
    int queryService(const String &service, const String &proto) { return queryService(service.c_str(), proto.c_str()); }
    String hostname(int idx);
    IPAddress address(int idx) { return IPAddress((uint32_t)bl_mdns_result(idx, BL_MDNS_IP, 0, 0)); }
    IPAddress IP(int idx) { return address(idx); }
    uint16_t port(int idx) { return (uint16_t)bl_mdns_result(idx, BL_MDNS_PORT, 0, 0); }
};

extern MDNSResponder MDNS;

/* ---------------- ArduinoOTA ----------------
 * Baseline uploads sketches over WiFi (not firmware images like Arduino's OTA):
 *   python tools/flick.py upload my_sketch --host NAME [--password PW]
 * Baseline's own task receives the upload, so handle() has nothing to do, and
 * because the board reboots into the new sketch the onStart/onEnd/onProgress/
 * onError callbacks are accepted but never called. Call begin() once WiFi is
 * connected. The default hostname is "baseline" (NAME.local). */
class ArduinoOTAClass {
public:
    typedef void (*THandlerFunction)();
    typedef void (*THandlerFunction_Progress)(unsigned int, unsigned int);
    typedef void (*THandlerFunction_Error)(int);

    ArduinoOTAClass &setHostname(const char *name) { host_ = name; return *this; }
    String getHostname() { return host_; }
    ArduinoOTAClass &setPassword(const char *password) { password_ = password; return *this; }
    ArduinoOTAClass &setPasswordHash(const char *) { return *this; }    // not supported: use setPassword
    ArduinoOTAClass &setPort(uint16_t port) { port_ = port; return *this; }
    ArduinoOTAClass &setMdnsEnabled(bool) { return *this; }
    ArduinoOTAClass &setRebootOnSuccess(bool) { return *this; }
    ArduinoOTAClass &onStart(THandlerFunction) { return *this; }
    ArduinoOTAClass &onEnd(THandlerFunction) { return *this; }
    ArduinoOTAClass &onProgress(THandlerFunction_Progress) { return *this; }
    ArduinoOTAClass &onError(THandlerFunction_Error) { return *this; }
    void begin() { bl_ota_begin(host_.c_str(), password_.length() ? password_.c_str() : 0, port_); }
    void end() { bl_ota_end(); }
    void handle() {}

private:
    String host_ = "baseline";
    String password_;
    uint16_t port_ = 3232;
};

extern ArduinoOTAClass ArduinoOTA;

#define OTA_AUTH_ERROR    0
#define OTA_BEGIN_ERROR   1
#define OTA_CONNECT_ERROR 2
#define OTA_RECEIVE_ERROR 3
#define OTA_END_ERROR     4

/* ---------------- BaselineUpdate (ABI 1.7) ----------------
 * The board fetches its own updates over HTTP(S) (a GitHub release, your
 * server, or `python -m http.server` on your PC):
 *   BaselineUpdate.sketchFromUrl("http://server/my_sketch.flick");
 *   BaselineUpdate.baselineFromUrl("http://server/baseline-<target>.bsu", sketchUrl);
 * Both restart the board on success and return only on failure (a BL_ERR_*
 * code; the console says why). A new sketch rolls back if it crashes on its
 * first run, a new Baseline (two-slot boards) if it fails to start.
 * mode(): BL_UPDATE_DUAL - 8/16 MB boards: two Baseline slots, the sketch and
 *           files are kept;
 *         BL_UPDATE_STAGED - 4 MB boards: the update erases the sketch and
 *           LittleFS, so pass sketchUrl to have Baseline install a sketch
 *           afterwards;
 *         BL_UPDATE_NONE - over USB only.
 * The PC can push the same updates: flick upload --host / flick baseline update --host. */
class BaselineUpdateClass {
public:
    int mode() { return bl_baseline_update_mode(); }
    const char *modeName()
    {
        int m = mode();
        return m == BL_UPDATE_DUAL ? "dual" : m == BL_UPDATE_STAGED ? "staged" : "none";
    }
    const char *baselineVersion() { return bl_baseline_version(); }
    int sketchFromUrl(const char *url) { return bl_ota_pull_sketch(url); }
    int baselineFromUrl(const char *url, const char *sketchUrl = nullptr) { return bl_ota_pull_baseline(url, sketchUrl); }
};

static BaselineUpdateClass BaselineUpdate __attribute__((unused));   // no state: unused copies cost nothing

#endif /* __cplusplus */
#endif /* BASELINE_NET_H */
