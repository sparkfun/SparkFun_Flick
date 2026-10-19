/*
 * baseline_mqtt.h - PubSubClient-compatible MQTT client. Included by
 * baseline_arduino.h; <PubSubClient.h> leads here too.
 *
 * Baseline's MQTT client (ESP-IDF esp-mqtt) does the networking. As with
 * PubSubClient, connect() blocks, there is no automatic reconnect, and the
 * callback runs inside loop(). Pass a WiFiClientSecure to use TLS (checked
 * against Baseline's CA bundle, or the certificate given to setCACert();
 * setInsecure() is not supported for MQTT). QoS 1 subscriptions and publishes
 * work; the default buffer is 1024 bytes.
 */
#ifndef BASELINE_MQTT_H
#define BASELINE_MQTT_H

#ifdef __cplusplus

#define MQTT_CONNECTION_TIMEOUT     BL_MQTT_CONNECTION_TIMEOUT
#define MQTT_CONNECTION_LOST        BL_MQTT_CONNECTION_LOST
#define MQTT_CONNECT_FAILED         BL_MQTT_CONNECT_FAILED
#define MQTT_DISCONNECTED           BL_MQTT_DISCONNECTED
#define MQTT_CONNECTED              BL_MQTT_CONNECTED
#define MQTT_CONNECT_BAD_PROTOCOL   BL_MQTT_CONNECT_BAD_PROTOCOL
#define MQTT_CONNECT_BAD_CLIENT_ID  BL_MQTT_CONNECT_BAD_CLIENT_ID
#define MQTT_CONNECT_UNAVAILABLE    BL_MQTT_CONNECT_UNAVAILABLE
#define MQTT_CONNECT_BAD_CREDENTIALS BL_MQTT_CONNECT_BAD_CREDENTIALS
#define MQTT_CONNECT_UNAUTHORIZED   BL_MQTT_CONNECT_UNAUTHORIZED

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_KEEPALIVE       15
#define MQTT_SOCKET_TIMEOUT  15

#define MQTT_CALLBACK_SIGNATURE void (*callback)(char *, uint8_t *, unsigned int)

class PubSubClient : public Print {
public:
    typedef void (*Callback)(char *topic, uint8_t *payload, unsigned int length);

    PubSubClient() {}
    explicit PubSubClient(WiFiClient &client) { setClient(client); }
    PubSubClient(IPAddress ip, uint16_t port, WiFiClient &client) { setServer(ip, port); setClient(client); }
    PubSubClient(const char *domain, uint16_t port, WiFiClient &client) { setServer(domain, port); setClient(client); }
    PubSubClient(IPAddress ip, uint16_t port, Callback cb, WiFiClient &client) { setServer(ip, port); setCallback(cb); setClient(client); }
    PubSubClient(const char *domain, uint16_t port, Callback cb, WiFiClient &client) { setServer(domain, port); setCallback(cb); setClient(client); }
    PubSubClient(const PubSubClient &) = delete;
    PubSubClient &operator=(const PubSubClient &) = delete;
    ~PubSubClient();

    PubSubClient &setServer(IPAddress ip, uint16_t port) { return setServer(ip.toString().c_str(), port); }
    PubSubClient &setServer(const uint8_t *ip, uint16_t port) { return setServer(IPAddress(ip[0], ip[1], ip[2], ip[3]), port); }
    PubSubClient &setServer(const char *domain, uint16_t port) { config(BL_MQTT_SERVER, domain, port); return *this; }
    PubSubClient &setCallback(Callback cb) { callback_ = cb; return *this; }
    PubSubClient &setClient(WiFiClient &client) { client_ = &client; return *this; }
    PubSubClient &setKeepAlive(uint16_t seconds) { config(BL_MQTT_KEEPALIVE, 0, seconds); return *this; }
    PubSubClient &setSocketTimeout(uint16_t seconds) { timeoutMs_ = seconds * 1000UL; return *this; }
    bool setBufferSize(uint16_t size);
    uint16_t getBufferSize() { return bufferSize_; }

    bool connect(const char *id) { return connect(id, 0, 0, 0, 0, false, 0, true); }
    bool connect(const char *id, const char *user, const char *pass) { return connect(id, user, pass, 0, 0, false, 0, true); }
    bool connect(const char *id, const char *willTopic, uint8_t willQos, bool willRetain, const char *willMessage)
    {
        return connect(id, 0, 0, willTopic, willQos, willRetain, willMessage, true);
    }
    bool connect(const char *id, const char *user, const char *pass, const char *willTopic, uint8_t willQos,
                 bool willRetain, const char *willMessage)
    {
        return connect(id, user, pass, willTopic, willQos, willRetain, willMessage, true);
    }
    bool connect(const char *id, const char *user, const char *pass, const char *willTopic, uint8_t willQos,
                 bool willRetain, const char *willMessage, bool cleanSession);
    void disconnect() { if (h_ >= 0) bl_mqtt_disconnect(h_); }

    bool publish(const char *topic, const char *payload) { return publish(topic, payload, false); }
    bool publish(const char *topic, const char *payload, bool retained)
    {
        return publish(topic, (const uint8_t *)payload, payload ? strlen(payload) : 0, retained);
    }
    bool publish(const char *topic, const uint8_t *payload, unsigned int length) { return publish(topic, payload, length, false); }
    bool publish(const char *topic, const uint8_t *payload, unsigned int length, bool retained)
    {
        return h_ >= 0 && bl_mqtt_publish(h_, topic, payload, length, 0, retained) == 1;
    }
    bool publish_P(const char *topic, const char *payload, bool retained) { return publish(topic, payload, retained); }
    bool publish_P(const char *topic, const uint8_t *payload, unsigned int length, bool retained)
    {
        return publish(topic, payload, length, retained);
    }
    /* Streamed publish: beginPublish(), print()/write(), endPublish(). */
    bool beginPublish(const char *topic, unsigned int plength, bool retained);
    int endPublish();
    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override;

    bool subscribe(const char *topic, uint8_t qos = 0) { return h_ >= 0 && bl_mqtt_subscribe(h_, topic, qos) == 1; }
    bool unsubscribe(const char *topic) { return h_ >= 0 && bl_mqtt_unsubscribe(h_, topic) == 1; }

    /* Delivers received messages to the callback. Call it often from loop(). */
    bool loop();
    bool connected() { return state() == MQTT_CONNECTED; }
    int state() { return h_ >= 0 ? bl_mqtt_state(h_) : MQTT_DISCONNECTED; }

private:
    bool ensure();
    void config(int what, const char *text, int32_t value)
    {
        if (ensure()) {
            bl_mqtt_config(h_, what, text, value);
        }
    }
    int h_ = -1;
    WiFiClient *client_ = 0;
    Callback callback_ = 0;
    uint32_t timeoutMs_ = MQTT_SOCKET_TIMEOUT * 1000UL;
    uint16_t bufferSize_ = MQTT_MAX_PACKET_SIZE;
    uint8_t *rx_ = 0;           // payload buffer for loop(), bufferSize_ + 1 bytes
    String pubTopic_;           // streamed publish in progress
    uint8_t *pubBuf_ = 0;
    size_t pubLen_ = 0;
    size_t pubCap_ = 0;
    bool pubRetain_ = false;
};

#endif /* __cplusplus */
#endif /* BASELINE_MQTT_H */
