/* sketch_mqtt.cpp - out-of-line parts of baseline_mqtt.h. */
#include "baseline.h"

#if defined(BL_HAS_WIFI)     /* the whole file: boards with this feature only */

#define TOPIC_MAX 256

PubSubClient::~PubSubClient()
{
    if (h_ >= 0) {
        bl_mqtt_free(h_);
    }
    free(rx_);
    free(pubBuf_);
}

bool PubSubClient::ensure()
{
    if (h_ < 0) {
        h_ = bl_mqtt_new();
        if (h_ >= 0) {
            bl_mqtt_config(h_, BL_MQTT_BUFFER_SIZE, 0, bufferSize_);
        }
    }
    return h_ >= 0;
}

bool PubSubClient::setBufferSize(uint16_t size)
{
    if (!size) {
        return false;
    }
    bufferSize_ = size;
    free(rx_);
    rx_ = 0;
    config(BL_MQTT_BUFFER_SIZE, 0, size);
    return true;
}

bool PubSubClient::connect(const char *id, const char *user, const char *pass, const char *willTopic,
                           uint8_t willQos, bool willRetain, const char *willMessage, bool cleanSession)
{
    if (!ensure()) {
        return false;
    }
    bl_mqtt_config(h_, BL_MQTT_CLIENT_ID, id, 0);
    bl_mqtt_config(h_, BL_MQTT_USERNAME, user, 0);
    bl_mqtt_config(h_, BL_MQTT_PASSWORD, pass, 0);
    bl_mqtt_config(h_, BL_MQTT_WILL_TOPIC, willTopic, 0);
    bl_mqtt_config(h_, BL_MQTT_WILL_MESSAGE, willMessage, 0);
    bl_mqtt_config(h_, BL_MQTT_WILL_QOS, 0, willQos);
    bl_mqtt_config(h_, BL_MQTT_WILL_RETAIN, 0, willRetain);
    bl_mqtt_config(h_, BL_MQTT_CLEAN_SESSION, 0, cleanSession);
    bool tls = client_ && client_->isSecure();
    bl_mqtt_config(h_, BL_MQTT_TLS, 0, tls);
    const char *ca = tls ? static_cast<WiFiClientSecure *>(client_)->ca_ : 0;
    bl_mqtt_config(h_, BL_MQTT_CA_CERT, ca, 0);
    return bl_mqtt_connect(h_, timeoutMs_) == 1;
}

bool PubSubClient::loop()
{
    if (h_ < 0) {
        return false;
    }
    if (!rx_) {
        rx_ = (uint8_t *)malloc((size_t)bufferSize_ + 1);
        if (!rx_) {
            return connected();
        }
    }
    char topic[TOPIC_MAX];
    int n;
    while ((n = bl_mqtt_take(h_, topic, sizeof(topic), rx_, bufferSize_)) >= 0) {
        unsigned int len = (unsigned int)n < bufferSize_ ? (unsigned int)n : bufferSize_;
        rx_[len] = 0;   // lets sketches treat the payload as text
        if (callback_) {
            callback_(topic, rx_, len);
        }
    }
    return connected();
}

bool PubSubClient::beginPublish(const char *topic, unsigned int plength, bool retained)
{
    if (!connected() || !topic) {
        return false;
    }
    free(pubBuf_);
    pubCap_ = plength ? plength : 64;
    pubBuf_ = (uint8_t *)malloc(pubCap_);
    pubLen_ = 0;
    pubTopic_ = topic;
    pubRetain_ = retained;
    return pubBuf_ != 0;
}

size_t PubSubClient::write(const uint8_t *buf, size_t size)
{
    if (!pubBuf_) {
        return 0;
    }
    if (pubLen_ + size > pubCap_) {
        size_t cap = (pubLen_ + size) * 2;
        uint8_t *p = (uint8_t *)realloc(pubBuf_, cap);
        if (!p) {
            return 0;
        }
        pubBuf_ = p;
        pubCap_ = cap;
    }
    memcpy(pubBuf_ + pubLen_, buf, size);
    pubLen_ += size;
    return size;
}

int PubSubClient::endPublish()
{
    if (!pubBuf_) {
        return 0;
    }
    bool ok = publish(pubTopic_.c_str(), pubBuf_, pubLen_, pubRetain_);
    free(pubBuf_);
    pubBuf_ = 0;
    pubLen_ = pubCap_ = 0;
    return ok ? 1 : 0;
}

#endif /* BL_HAS_WIFI */
