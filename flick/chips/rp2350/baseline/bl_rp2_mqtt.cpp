/*
 * bl_rp2_mqtt.cpp - the MQTT entries (ABI 1.4) for RP2350 boards with WiFi: a
 * small MQTT 3.1.1 client on arduino-pico's WiFiClient / WiFiClientSecure.
 * It behaves as the ESP32 Baselines' (baseline/src/bl_impl_mqtt.cpp) and as
 * PubSubClient: connect() blocks until connected or failed, there is no
 * automatic reconnect, QoS 0 and 1 (no waiting for PUBACK/SUBACK), and
 * received messages wait in a queue until the sketch's client.loop() takes
 * them. Keepalive pings and reading run from bl_poll().
 */
#include "bl_rp2.h"

#if defined(BL_HAS_WIFI)
#include <WiFi.h>
#include <WiFiClientSecure.h>

#define MAX_MQTT        2
#define QUEUE_LEN       16

/* bl_rp2_net.cpp */
void bl_net_use_ca_bundle(WiFiClientSecure *c);

struct MqttMsg {
    char *topic;
    uint8_t *data;
    size_t len;
};

struct MqttSlot {
    bool used;
    WiFiClient *client;
    BearSSL::X509List *ca;
    int state;
    String host, client_id, username, password, will_topic, will_message, ca_cert;
    uint16_t port;
    int keepalive, will_qos, will_retain, buffer_size;
    bool clean_session, tls;
    uint16_t next_id;
    uint32_t last_out, last_in;
    bool ping_out;
    // the packet being read
    uint8_t *rx;
    uint8_t rx_header;
    uint32_t rx_len, rx_got;
    int rx_len_bytes;           // 0: next byte is a header; >0: reading the length; -1: reading the body
    uint32_t rx_mult;
    MqttMsg queue[QUEUE_LEN];
    uint8_t q_head, q_tail;
};
static MqttSlot s_mqtt[MAX_MQTT];

static MqttSlot *slot_of(int h)
{
    return (h >= 0 && h < MAX_MQTT && s_mqtt[h].used) ? &s_mqtt[h] : NULL;
}

/* Calls on a TLS client run on Baseline's big stack. */
template <typename F> static void io(MqttSlot *s, F &&f)
{
    if (s->tls) {
        on_big_stack(f);
    } else {
        f();
    }
}

static bool send(MqttSlot *s, const uint8_t *data, size_t len)
{
    size_t n = 0;
    io(s, [&] { n = s->client->write(data, len); });
    s->last_out = millis();
    return n == len;
}

/* Fixed header + remaining length, then the parts. */
static bool send_packet(MqttSlot *s, uint8_t header, const uint8_t *body, size_t body_len, const uint8_t *tail = NULL,
                        size_t tail_len = 0)
{
    uint8_t head[5];
    size_t n = 0;
    uint32_t rem = body_len + tail_len;
    head[n++] = header;
    do {
        uint8_t b = rem % 128;
        rem /= 128;
        head[n++] = rem ? (b | 0x80) : b;
    } while (rem && n < sizeof(head));
    // One buffer: TLS sends a record per write.
    size_t total = n + body_len + tail_len;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        return false;
    }
    memcpy(buf, head, n);
    if (body_len) {
        memcpy(buf + n, body, body_len);
    }
    if (tail_len) {
        memcpy(buf + n + body_len, tail, tail_len);
    }
    bool ok = send(s, buf, total);
    free(buf);
    return ok;
}

/* A growing body: MQTT strings are a 2-byte length plus the bytes. */
struct Body {
    uint8_t *p = nullptr;
    size_t len = 0, cap = 0;
    bool ok = true;
    ~Body() { free(p); }
    void put(const void *data, size_t n)
    {
        if (len + n > cap) {
            size_t want = cap ? cap * 2 : 128;
            while (want < len + n) {
                want *= 2;
            }
            uint8_t *bigger = (uint8_t *)realloc(p, want);
            if (!bigger) {
                ok = false;
                return;
            }
            p = bigger;
            cap = want;
        }
        memcpy(p + len, data, n);
        len += n;
    }
    void byte(uint8_t b) { put(&b, 1); }
    void u16(uint16_t v) { byte(v >> 8); byte(v & 0xFF); }
    void str(const char *s, size_t n) { u16((uint16_t)n); put(s, n); }
    void str(const String &s) { str(s.c_str(), s.length()); }
};

static void free_msg(MqttMsg &m)
{
    free(m.topic);
    free(m.data);
    m = {};
}

static void drop_client(MqttSlot *s)
{
    if (s->client) {
        io(s, [&] { s->client->stop(); });
        delete s->client;
        s->client = NULL;
    }
    delete s->ca;
    s->ca = NULL;
    s->rx_len_bytes = 0;
}

static void lost(MqttSlot *s)
{
    drop_client(s);
    if (s->state == BL_MQTT_CONNECTED) {
        s->state = BL_MQTT_CONNECTION_LOST;
    }
}

static void on_publish(MqttSlot *s)
{
    uint8_t *p = s->rx;
    uint32_t len = s->rx_got;
    if (len < 2) {
        return;
    }
    uint16_t tlen = (uint16_t)(p[0] << 8 | p[1]);
    int qos = (s->rx_header >> 1) & 3;
    uint32_t at = 2 + tlen;
    uint16_t id = 0;
    if (qos) {
        if (at + 2 > len) {
            return;
        }
        id = (uint16_t)(p[at] << 8 | p[at + 1]);
        at += 2;
    }
    if (at > len) {
        return;
    }
    uint8_t next = (uint8_t)((s->q_head + 1) % QUEUE_LEN);
    if (next != s->q_tail) {        // a full queue drops the message: the sketch isn't calling loop()
        MqttMsg &m = s->queue[s->q_head];
        m.topic = (char *)malloc(tlen + 1);
        m.len = len - at;
        m.data = (uint8_t *)malloc(m.len ? m.len : 1);
        if (m.topic && m.data) {
            memcpy(m.topic, p + 2, tlen);
            m.topic[tlen] = '\0';
            memcpy(m.data, p + at, m.len);
            s->q_head = next;
        } else {
            free_msg(m);
        }
    }
    if (qos == 1) {
        uint8_t ack[2] = { (uint8_t)(id >> 8), (uint8_t)id };
        send_packet(s, 0x40, ack, 2);       // PUBACK
    }
}

/* Reads what has arrived; one whole packet per call to on_packet. Returns the
 * type of the last whole packet, or 0. */
static int read_packets(MqttSlot *s)
{
    int last = 0;
    if (!s->client) {
        return 0;
    }
    io(s, [&] {
        uint8_t buf[128];
        for (int rounds = 0; rounds < 16; rounds++) {
            int avail = s->client->available();
            if (avail <= 0) {
                break;
            }
            int n = s->client->read(buf, avail < (int)sizeof(buf) ? avail : (int)sizeof(buf));
            for (int i = 0; i < n; i++) {
                uint8_t b = buf[i];
                if (s->rx_len_bytes == 0) {
                    s->rx_header = b;
                    s->rx_len = 0;
                    s->rx_mult = 1;
                    s->rx_got = 0;
                    s->rx_len_bytes = 1;
                    continue;
                }
                if (s->rx_len_bytes > 0) {
                    s->rx_len += (b & 0x7F) * s->rx_mult;
                    s->rx_mult *= 128;
                    if (b & 0x80 && s->rx_len_bytes < 4) {
                        s->rx_len_bytes++;
                        continue;
                    }
                    s->rx_len_bytes = -1;
                    if (s->rx_len) {
                        continue;
                    }
                } else {
                    if (s->rx_got < (uint32_t)s->buffer_size) {
                        s->rx[s->rx_got] = b;
                    }
                    s->rx_got++;
                    if (s->rx_got < s->rx_len) {
                        continue;
                    }
                }
                // a whole packet
                s->rx_len_bytes = 0;
                s->last_in = millis();
                int type = s->rx_header >> 4;
                if (type == 3 && s->rx_got <= (uint32_t)s->buffer_size) {
                    on_publish(s);      // a message bigger than the buffer is dropped, as in PubSubClient
                } else if (type == 13) {
                    s->ping_out = false;
                }
                last = type;
            }
        }
    });
    return last;
}

static void poll_slot(MqttSlot *s)
{
    if (!s->client || s->state != BL_MQTT_CONNECTED) {
        return;
    }
    bool up = false;
    io(s, [&] { up = s->client->connected(); });
    if (!up) {
        lost(s);
        return;
    }
    read_packets(s);
    uint32_t ka = (uint32_t)s->keepalive * 1000;
    if (!ka) {
        return;
    }
    uint32_t now = millis();
    if (s->ping_out && now - s->last_in > ka + ka / 2) {
        lost(s);        // no PINGRESP: the broker or the network is gone
    } else if (!s->ping_out && (now - s->last_out >= ka || now - s->last_in >= ka)) {
        if (send_packet(s, 0xC0, NULL, 0)) {     // PINGREQ
            s->ping_out = true;
            s->last_in = now;
        } else {
            lost(s);
        }
    }
}

void bl_mqtt_poll(void)
{
    for (int h = 0; h < MAX_MQTT; h++) {
        if (s_mqtt[h].used) {
            poll_slot(&s_mqtt[h]);
        }
    }
}

extern "C" int bl_impl_mqtt_new(void)
{
    for (int h = 0; h < MAX_MQTT; h++) {
        if (!s_mqtt[h].used) {
            MqttSlot &s = s_mqtt[h];
            s = MqttSlot();
            s.used = true;
            s.port = 1883;
            s.keepalive = 15;           // PubSubClient's default
            s.buffer_size = 1024;
            s.clean_session = true;
            s.state = BL_MQTT_DISCONNECTED;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_mqtt_free(int h)
{
    MqttSlot *s = slot_of(h);
    if (!s) {
        return;
    }
    drop_client(s);
    while (s->q_tail != s->q_head) {
        free_msg(s->queue[s->q_tail]);
        s->q_tail = (uint8_t)((s->q_tail + 1) % QUEUE_LEN);
    }
    free(s->rx);
    *s = MqttSlot();
}

extern "C" int bl_impl_mqtt_config(int h, int what, const char *text, int32_t value)
{
    MqttSlot *s = slot_of(h);
    if (!s) {
        return BL_ERR_ARG;
    }
    String t = text ? text : "";
    switch (what) {
    case BL_MQTT_SERVER:        s->host = t; if (value > 0) s->port = (uint16_t)value; break;
    case BL_MQTT_CLIENT_ID:     s->client_id = t; break;
    case BL_MQTT_USERNAME:      s->username = t; break;
    case BL_MQTT_PASSWORD:      s->password = t; break;
    case BL_MQTT_KEEPALIVE:     s->keepalive = value; break;
    case BL_MQTT_WILL_TOPIC:    s->will_topic = t; break;
    case BL_MQTT_WILL_MESSAGE:  s->will_message = t; break;
    case BL_MQTT_WILL_QOS:      s->will_qos = value; break;
    case BL_MQTT_WILL_RETAIN:   s->will_retain = value; break;
    case BL_MQTT_CLEAN_SESSION: s->clean_session = value != 0; break;
    case BL_MQTT_TLS:           s->tls = value != 0; break;
    case BL_MQTT_CA_CERT:       s->ca_cert = t; break;
    case BL_MQTT_BUFFER_SIZE:   s->buffer_size = value > 0 ? value : 1024; free(s->rx); s->rx = NULL; break;
    default:                    return BL_ERR_ARG;
    }
    return BL_OK;
}

extern "C" int bl_impl_mqtt_connect(int h, uint32_t timeout_ms)
{
    MqttSlot *s = slot_of(h);
    if (!s || s->host.isEmpty()) {
        return 0;
    }
    drop_client(s);
    if (!s->rx && !(s->rx = (uint8_t *)malloc(s->buffer_size))) {
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }
    if (WiFi.status() != WL_CONNECTED) {
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }
    if (s->tls) {
        WiFiClientSecure *c = new WiFiClientSecure();
        if (s->ca_cert.length()) {
            s->ca = new BearSSL::X509List(s->ca_cert.c_str());
            c->setTrustAnchors(s->ca);
        } else {
            bl_net_use_ca_bundle(c);
        }
        s->client = c;
    } else {
        s->client = new WiFiClient();
    }
    uint32_t limit = timeout_ms ? timeout_ms : 15000;
    BlWdogLong wdt;
    bool connected = false;
    io(s, [&] {
        s->client->setTimeout(limit);
        connected = s->client->connect(s->host.c_str(), s->port);
    });
    if (!connected) {
        drop_client(s);
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }

    // CONNECT
    Body b;
    b.str("MQTT", 4);
    b.byte(4);                      // protocol level 3.1.1
    uint8_t flags = s->clean_session ? 0x02 : 0;
    if (s->will_topic.length()) {
        flags |= 0x04 | (uint8_t)((s->will_qos & 3) << 3) | (s->will_retain ? 0x20 : 0);
    }
    if (s->username.length()) {
        flags |= 0x80;
        if (s->password.length()) {
            flags |= 0x40;
        }
    }
    b.byte(flags);
    b.u16((uint16_t)s->keepalive);
    b.str(s->client_id);
    if (s->will_topic.length()) {
        b.str(s->will_topic);
        b.str(s->will_message);
    }
    if (s->username.length()) {
        b.str(s->username);
        if (s->password.length()) {
            b.str(s->password);
        }
    }
    if (!b.ok || !send_packet(s, 0x10, b.p, b.len)) {
        drop_client(s);
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }

    // CONNACK
    s->rx_len_bytes = 0;
    uint32_t start = millis();
    while (millis() - start < limit) {
        if (read_packets(s) == 2) {
            uint8_t code = s->rx_got >= 2 ? s->rx[1] : 0xFF;
            if (code == 0) {
                s->state = BL_MQTT_CONNECTED;
                s->last_in = s->last_out = millis();
                s->ping_out = false;
                return 1;
            }
            drop_client(s);
            s->state = code <= 5 ? code : BL_MQTT_CONNECT_FAILED;     // 1..5, as PubSubClient
            return 0;
        }
        bool up = false;
        io(s, [&] { up = s->client->connected(); });
        if (!up) {
            drop_client(s);
            s->state = BL_MQTT_CONNECT_FAILED;
            return 0;
        }
        delay(5);
    }
    drop_client(s);
    s->state = BL_MQTT_CONNECTION_TIMEOUT;
    return 0;
}

extern "C" void bl_impl_mqtt_disconnect(int h)
{
    MqttSlot *s = slot_of(h);
    if (!s) {
        return;
    }
    if (s->client && s->state == BL_MQTT_CONNECTED) {
        send_packet(s, 0xE0, NULL, 0);      // DISCONNECT
    }
    drop_client(s);
    s->state = BL_MQTT_DISCONNECTED;
}

extern "C" int bl_impl_mqtt_state(int h)
{
    MqttSlot *s = slot_of(h);
    if (!s) {
        return BL_MQTT_DISCONNECTED;
    }
    poll_slot(s);
    return s->state;
}

static bool ready(MqttSlot *s, const char *topic)
{
    return s && s->client && s->state == BL_MQTT_CONNECTED && topic;
}

extern "C" int bl_impl_mqtt_publish(int h, const char *topic, const uint8_t *payload, size_t len, int qos, int retain)
{
    MqttSlot *s = slot_of(h);
    if (!ready(s, topic)) {
        return 0;
    }
    Body b;
    b.str(topic, strlen(topic));
    if (qos > 0) {
        b.u16(++s->next_id ? s->next_id : ++s->next_id);
    }
    if (!b.ok) {
        return 0;
    }
    uint8_t header = (uint8_t)(0x30 | ((qos > 0 ? 1 : 0) << 1) | (retain ? 1 : 0));
    return send_packet(s, header, b.p, b.len, payload, payload ? len : 0) ? 1 : 0;
}

extern "C" int bl_impl_mqtt_subscribe(int h, const char *topic, int qos)
{
    MqttSlot *s = slot_of(h);
    if (!ready(s, topic)) {
        return 0;
    }
    Body b;
    b.u16(++s->next_id ? s->next_id : ++s->next_id);
    b.str(topic, strlen(topic));
    b.byte((uint8_t)(qos > 0 ? 1 : 0));
    return b.ok && send_packet(s, 0x82, b.p, b.len) ? 1 : 0;
}

extern "C" int bl_impl_mqtt_unsubscribe(int h, const char *topic)
{
    MqttSlot *s = slot_of(h);
    if (!ready(s, topic)) {
        return 0;
    }
    Body b;
    b.u16(++s->next_id ? s->next_id : ++s->next_id);
    b.str(topic, strlen(topic));
    return b.ok && send_packet(s, 0xA2, b.p, b.len) ? 1 : 0;
}

extern "C" int bl_impl_mqtt_take(int h, char *topic, size_t topic_size, uint8_t *payload, size_t payload_size)
{
    MqttSlot *s = slot_of(h);
    if (!s) {
        return -1;
    }
    poll_slot(s);
    if (s->q_tail == s->q_head) {
        return -1;
    }
    MqttMsg &m = s->queue[s->q_tail];
    if (topic && topic_size) {
        strlcpy(topic, m.topic, topic_size);
    }
    if (payload && payload_size) {
        memcpy(payload, m.data, m.len < payload_size ? m.len : payload_size);
    }
    int len = (int)m.len;
    free_msg(m);
    s->q_tail = (uint8_t)((s->q_tail + 1) % QUEUE_LEN);
    return len;
}

#else
void bl_mqtt_poll(void)
{
}
#endif /* BL_HAS_WIFI */
