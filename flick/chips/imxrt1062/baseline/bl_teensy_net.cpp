/* API table: Ethernet (ABI 1.9) and the network calls over it, on the Teensy 4.1
 * (its DP83825 PHY), with Teensyduino's NativeEthernet (FNET).
 *
 * The same tcp_*, udp_*, net_resolve and http_* entries the ESP32 family runs
 * over WiFi, so the SDK's WiFiClient/WiFiUDP/HTTPClient classes (and Ethernet.h's
 * EthernetClient, EthernetUDP, ...) work unchanged. Objects live in handle
 * tables here; sketches hold small integers. No TLS: http_* to https:// URLs
 * return BL_ERR_UNSUPPORTED. Other Teensy boards: the weak stubs.
 */
#include "bl_teensy.h"

#if defined(ARDUINO_TEENSY41)
#include <NativeEthernet.h>
#include <NativeDns.h>

static bool s_started;

static uint32_t ip_u32(const IPAddress &a)
{
    return (uint32_t)a[0] | ((uint32_t)a[1] << 8) | ((uint32_t)a[2] << 16) | ((uint32_t)a[3] << 24);
}

static IPAddress ip_of(uint32_t v)
{
    return IPAddress(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24);
}

/* The Teensy 4.1's own MAC address, in its fuses (OCOTP). */
static void own_mac(uint8_t *mac)
{
    uint32_t m1 = HW_OCOTP_MAC1, m0 = HW_OCOTP_MAC0;
    mac[0] = m1 >> 8;
    mac[1] = m1;
    mac[2] = m0 >> 24;
    mac[3] = m0 >> 16;
    mac[4] = m0 >> 8;
    mac[5] = m0;
}

/* ---------------- Ethernet ---------------- */

extern "C" int bl_impl_eth_begin(const uint8_t *mac, uint32_t ip, uint32_t dns, uint32_t gateway, uint32_t subnet,
                                 uint32_t timeout_ms)
{
    uint8_t m[6];
    if (mac) {
        memcpy(m, mac, 6);
    } else {
        own_mac(m);
    }
    BlWdogLong long_watchdog;           /* DHCP can take seconds */
    if (ip == 0) {
        if (!Ethernet.begin(m, timeout_ms ? timeout_ms : 15000, 4000)) {
            return 0;
        }
    } else {
        IPAddress a = ip_of(ip);
        IPAddress d = dns ? ip_of(dns) : IPAddress(a[0], a[1], a[2], 1);
        IPAddress g = gateway ? ip_of(gateway) : IPAddress(a[0], a[1], a[2], 1);
        IPAddress s = subnet ? ip_of(subnet) : IPAddress(255, 255, 255, 0);
        Ethernet.begin(m, a, d, g, s);
    }
    s_started = true;
    return ip_u32(Ethernet.localIP()) != 0 ? 1 : 0;
}

extern "C" int bl_impl_eth_status(int what)
{
    if (what == BL_ETH_HARDWARE) {
        return Ethernet.hardwareStatus() != EthernetNoHardware ? 1 : 0;
    }
    if (what == BL_ETH_LINK) {
        EthernetLinkStatus s = Ethernet.linkStatus();
        return s == LinkON ? 1 : s == LinkOFF ? 0 : -1;
    }
    return BL_ERR_ARG;
}

extern "C" uint32_t bl_impl_eth_ip(int which)
{
    if (!s_started) {
        return 0;
    }
    switch (which) {
    case BL_IP_STA:     return ip_u32(Ethernet.localIP());
    case BL_IP_GATEWAY: return ip_u32(Ethernet.gatewayIP());
    case BL_IP_SUBNET:  return ip_u32(Ethernet.subnetMask());
    case BL_IP_DNS:     return ip_u32(Ethernet.dnsServerIP());
    default:            return 0;
    }
}

extern "C" int bl_impl_eth_mac(uint8_t *mac)
{
    if (!mac) {
        return BL_ERR_ARG;
    }
    if (s_started) {
        Ethernet.MACAddress(mac);
    } else {
        own_mac(mac);
    }
    return BL_OK;
}

extern "C" int bl_impl_eth_maintain(void)
{
    return s_started ? Ethernet.maintain() : 0;
}

/* The network calls below need Ethernet started: like WiFi not connected. */
static bool net_up(void)
{
    return s_started && ip_u32(Ethernet.localIP()) != 0;
}

extern "C" int bl_impl_net_resolve(const char *host, uint32_t *ip)
{
    if (!host || !ip) {
        return BL_ERR_ARG;
    }
    if (!net_up()) {
        return BL_ERR_STATE;
    }
    IPAddress a;
    if (a.fromString(host)) {
        *ip = ip_u32(a);
        return BL_OK;
    }
    DNSClient dns;
    dns.begin(Ethernet.dnsServerIP());
    BlWdogLong long_watchdog;
    if (dns.getHostByName(host, a, 5000) != 1) {
        return BL_ERR_FAIL;
    }
    *ip = ip_u32(a);
    return BL_OK;
}

/* ---------------- TCP clients ---------------- */

#define MAX_CLIENTS 8
struct ClientSlot {
    EthernetClient client;
    int refs;
};
static ClientSlot s_clients[MAX_CLIENTS];

static ClientSlot *client_at(int h)
{
    return h >= 0 && h < MAX_CLIENTS && s_clients[h].refs > 0 ? &s_clients[h] : NULL;
}

static int client_new(void)
{
    for (int h = 0; h < MAX_CLIENTS; h++) {
        if (s_clients[h].refs == 0) {
            s_clients[h].client = EthernetClient();
            s_clients[h].refs = 1;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_tcp_client_new(int secure)
{
    return secure ? BL_ERR_UNSUPPORTED : client_new();     /* no TLS over Ethernet */
}

extern "C" void bl_impl_tcp_client_retain(int h)
{
    if (client_at(h)) {
        s_clients[h].refs++;
    }
}

extern "C" void bl_impl_tcp_client_release(int h)
{
    ClientSlot *c = client_at(h);
    if (c && --c->refs == 0) {
        c->client.stop();
    }
}

extern "C" int bl_impl_tcp_client_tls_config(int h, int insecure, const char *ca_pem)
{
    return BL_ERR_UNSUPPORTED;
}

extern "C" int bl_impl_tcp_client_connect(int h, const char *host, uint16_t port, int32_t timeout_ms)
{
    ClientSlot *c = client_at(h);
    if (!c || !host || !net_up()) {
        return 0;
    }
    c->client.setConnectionTimeout(timeout_ms > 0 && timeout_ms < 65535 ? timeout_ms : 3000);
    BlWdogLong long_watchdog;
    return c->client.connect(host, port) == 1 ? 1 : 0;
}

extern "C" int bl_impl_tcp_client_write(int h, const uint8_t *data, size_t len)
{
    ClientSlot *c = client_at(h);
    return c && data ? (int)c->client.write(data, len) : 0;
}

extern "C" int bl_impl_tcp_client_available(int h)
{
    ClientSlot *c = client_at(h);
    return c ? c->client.available() : 0;
}

extern "C" int bl_impl_tcp_client_read(int h, uint8_t *buf, size_t size)
{
    ClientSlot *c = client_at(h);
    if (!c || !buf || c->client.available() <= 0) {
        return -1;
    }
    return c->client.read(buf, size);
}

extern "C" int bl_impl_tcp_client_peek(int h)
{
    ClientSlot *c = client_at(h);
    return c ? c->client.peek() : -1;
}

extern "C" int bl_impl_tcp_client_connected(int h)
{
    ClientSlot *c = client_at(h);
    return c && (c->client.connected() || c->client.available() > 0) ? 1 : 0;
}

extern "C" void bl_impl_tcp_client_stop(int h)
{
    ClientSlot *c = client_at(h);
    if (c) {
        c->client.stop();
    }
}

extern "C" int bl_impl_tcp_client_remote(int h, uint32_t *ip, uint16_t *port)
{
    ClientSlot *c = client_at(h);
    if (!c) {
        return BL_ERR_ARG;
    }
    if (ip) {
        *ip = ip_u32(c->client.remoteIP());
    }
    if (port) {
        *port = c->client.remotePort();
    }
    return BL_OK;
}

/* ---------------- TCP servers ---------------- */

#define MAX_SERVERS 4
static EthernetServer *s_servers[MAX_SERVERS];

extern "C" int bl_impl_tcp_server_new(uint16_t port, int max_clients)
{
    if (!net_up()) {
        return BL_ERR_STATE;
    }
    for (int h = 0; h < MAX_SERVERS; h++) {
        if (!s_servers[h]) {
            s_servers[h] = new EthernetServer(port);
            if (!s_servers[h]) {
                return BL_ERR_NO_MEM;
            }
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
    EthernetClient incoming = s_servers[h]->accept();
    if (!incoming) {
        return -1;
    }
    int c = client_new();
    if (c < 0) {
        incoming.stop();
        return -1;
    }
    s_clients[c].client = incoming;
    return c;
}

extern "C" void bl_impl_tcp_server_close(int h)
{
    /* NativeEthernet has no way to stop listening: the slot is kept, so a later
     * tcp_server_new on the same port reuses nothing it can't. */
    (void)h;
}

/* ---------------- UDP ---------------- */

#define MAX_UDP 4
static EthernetUDP s_udp[MAX_UDP];
static bool s_udp_used[MAX_UDP];

static EthernetUDP *udp_at(int h)
{
    return h >= 0 && h < MAX_UDP && s_udp_used[h] ? &s_udp[h] : NULL;
}

extern "C" int bl_impl_udp_new(uint16_t port)
{
    if (!net_up()) {
        return BL_ERR_STATE;
    }
    for (int h = 0; h < MAX_UDP; h++) {
        if (!s_udp_used[h]) {
            if (!s_udp[h].begin(port ? port : (uint16_t)(49152 + h))) {
                return BL_ERR_FAIL;
            }
            s_udp_used[h] = true;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_udp_close(int h)
{
    EthernetUDP *u = udp_at(h);
    if (u) {
        u->stop();
        s_udp_used[h] = false;
    }
}

extern "C" int bl_impl_udp_begin_packet(int h, const char *host, uint16_t port)
{
    EthernetUDP *u = udp_at(h);
    return u && host ? u->beginPacket(host, port) : 0;
}

extern "C" int bl_impl_udp_write(int h, const uint8_t *data, size_t len)
{
    EthernetUDP *u = udp_at(h);
    return u && data ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_udp_end_packet(int h)
{
    EthernetUDP *u = udp_at(h);
    return u ? u->endPacket() : 0;
}

extern "C" int bl_impl_udp_parse_packet(int h)
{
    EthernetUDP *u = udp_at(h);
    return u ? u->parsePacket() : 0;
}

extern "C" int bl_impl_udp_read(int h, uint8_t *buf, size_t size)
{
    EthernetUDP *u = udp_at(h);
    return u && buf ? u->read(buf, size) : -1;
}

extern "C" int bl_impl_udp_remote(int h, uint32_t *ip, uint16_t *port)
{
    EthernetUDP *u = udp_at(h);
    if (!u) {
        return BL_ERR_ARG;
    }
    if (ip) {
        *ip = ip_u32(u->remoteIP());
    }
    if (port) {
        *port = u->remotePort();
    }
    return BL_OK;
}

/* ---------------- HTTP client (plain HTTP/1.1 over a TCP client) ---------------- */

/* "http://host[:port]/path" -> its parts. 0 for an https:// or malformed URL. */
static int split_url(const char *url, char *host, size_t host_size, uint16_t *port, const char **path)
{
    if (strncmp(url, "https://", 8) == 0) {
        return BL_ERR_UNSUPPORTED;              /* no TLS over Ethernet */
    }
    if (strncmp(url, "http://", 7) != 0) {
        return BL_ERR_ARG;
    }
    const char *h = url + 7;
    const char *end = h + strcspn(h, ":/");
    size_t n = (size_t)(end - h);
    if (n == 0 || n >= host_size) {
        return BL_ERR_ARG;
    }
    memcpy(host, h, n);
    host[n] = '\0';
    *port = 80;
    if (*end == ':') {
        *port = (uint16_t)strtoul(end + 1, NULL, 10);
        end += strcspn(end, "/");
    }
    *path = *end ? end : "/";
    return BL_OK;
}

/* A line of the response head (CRLF stripped), or false if the connection ended. */
static bool read_line(EthernetClient &c, char *line, size_t size, uint32_t deadline)
{
    size_t n = 0;
    while (millis() < deadline) {
        if (c.available() <= 0) {
            if (!c.connected()) {
                break;
            }
            bl_poll();
            continue;
        }
        int ch = c.read();
        if (ch == '\n') {
            line[n] = '\0';
            if (n && line[n - 1] == '\r') {
                line[n - 1] = '\0';
            }
            return true;
        }
        if (n < size - 1) {
            line[n++] = (char)ch;
        }
    }
    line[n] = '\0';
    return n > 0;
}

/* Append up to want bytes (want < 0: until the connection closes). */
static bool read_body(EthernetClient &c, char **buf, size_t *len, size_t *cap, long want, uint32_t deadline)
{
    while ((want < 0 || want > 0) && millis() < deadline) {
        int avail = c.available();
        if (avail <= 0) {
            if (!c.connected()) {
                return want <= 0;
            }
            bl_poll();
            continue;
        }
        size_t take = want >= 0 && (long)avail > want ? (size_t)want : (size_t)avail;
        if (*len + take + 1 > *cap) {
            size_t ncap = (*len + take + 1) * 2;
            char *nb = (char *)realloc(*buf, ncap);
            if (!nb) {
                return false;
            }
            *buf = nb;
            *cap = ncap;
        }
        int got = c.read((uint8_t *)*buf + *len, take);
        if (got > 0) {
            *len += (size_t)got;
            if (want > 0) {
                want -= got;
            }
        }
    }
    return want <= 0;
}

static int http_do(const char *method, const char *url, const char *headers, const uint8_t *body, size_t body_len,
                   char **response, size_t *response_len)
{
    char host[128], line[512];
    uint16_t port;
    const char *path;
    int r = split_url(url, host, sizeof(host), &port, &path);
    if (r != BL_OK) {
        return r;
    }
    if (!net_up()) {
        return BL_ERR_STATE;
    }
    BlWdogLong long_watchdog;
    EthernetClient c;
    c.setConnectionTimeout(5000);
    if (c.connect(host, port) != 1) {
        return BL_ERR_FAIL;
    }
    c.printf("%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: Flick\r\n", method, path, host);
    if (headers) {
        c.print(headers);
    }
    if (body_len || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0) {
        c.printf("Content-Length: %u\r\n", (unsigned)body_len);
    }
    c.print("\r\n");
    if (body && body_len) {
        c.write(body, body_len);
    }
    uint32_t deadline = millis() + 15000;
    if (!read_line(c, line, sizeof(line), deadline) || strncmp(line, "HTTP/", 5) != 0) {
        c.stop();
        return BL_ERR_FAIL;
    }
    int status = atoi(strchr(line, ' ') ? strchr(line, ' ') + 1 : "0");
    long content_length = -1;
    bool chunked = false;
    while (read_line(c, line, sizeof(line), deadline) && line[0]) {
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atol(line + 15);
        } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && strstr(line + 18, "chunked")) {
            chunked = true;
        }
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;
    if (chunked) {
        for (;;) {
            if (!read_line(c, line, sizeof(line), deadline)) {
                ok = false;
                break;
            }
            long size = strtol(line, NULL, 16);
            if (size <= 0) {
                break;
            }
            ok = read_body(c, &buf, &len, &cap, size, deadline) && read_line(c, line, sizeof(line), deadline);
            if (!ok) {
                break;
            }
        }
    } else if (strcmp(method, "HEAD") != 0 && status != 204 && status != 304) {
        ok = read_body(c, &buf, &len, &cap, content_length, deadline);
    }
    c.stop();
    if (!ok && !buf) {
        return BL_ERR_TIMEOUT;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return BL_ERR_NO_MEM;
        }
    }
    buf[len] = '\0';
    if (response) {
        *response = buf;
    } else {
        free(buf);
    }
    if (response_len) {
        *response_len = len;
    }
    return status;
}

extern "C" int bl_impl_http_request(const char *method, const char *url, const char *headers, const uint8_t *body,
                                    size_t body_len, char **response, size_t *response_len)
{
    if (response) {
        *response = NULL;
    }
    if (response_len) {
        *response_len = 0;
    }
    if (!method || !url) {
        return BL_ERR_ARG;
    }
    return http_do(method, url, headers, body, body_len, response, response_len);
}

extern "C" int bl_impl_http_get(const char *url, char *body, size_t size, int *status)
{
    char *resp = NULL;
    size_t len = 0;
    int st = http_do("GET", url, NULL, NULL, 0, &resp, &len);
    if (st < 0) {
        return st;
    }
    if (status) {
        *status = st;
    }
    size_t n = 0;
    if (body && size) {
        n = len < size - 1 ? len : size - 1;
        memcpy(body, resp, n);
        body[n] = '\0';
    }
    free(resp);
    return (int)n;
}

#endif /* ARDUINO_TEENSY41 */
