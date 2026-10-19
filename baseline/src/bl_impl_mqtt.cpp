/* API table: MQTT client on ESP-IDF's esp-mqtt (ABI 1.4). Behaves like
 * PubSubClient: connect() blocks until connected or failed, there is no
 * automatic reconnect, and received messages wait in a queue until the
 * sketch's client.loop() takes them. */
#include <Arduino.h>
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "gen/bl_impl.h"
#include "sketch_loader.h"

#define MAX_MQTT        2
#define MSG_QUEUE_LEN   16
#define BIT_CONNECTED   BIT0
#define BIT_DONE        BIT1

struct MqttMsg {
    char *topic;
    uint8_t *data;
    size_t len;
};

struct MqttSlot {
    bool used;
    esp_mqtt_client_handle_t client;
    QueueHandle_t messages;
    EventGroupHandle_t events;
    volatile int state;
    // settings (kept alive for the client's lifetime)
    String host, client_id, username, password, will_topic, will_message, ca_cert;
    uint16_t port;
    int keepalive, will_qos, will_retain, buffer_size;
    bool clean_session, tls;
    // a message arriving in several pieces
    MqttMsg partial;
    size_t partial_total;
};
static MqttSlot s_mqtt[MAX_MQTT];

static MqttSlot *slot_of(int h)
{
    return (h >= 0 && h < MAX_MQTT && s_mqtt[h].used) ? &s_mqtt[h] : NULL;
}

static void free_msg(MqttMsg &m)
{
    free(m.topic);
    free(m.data);
    m = {};
}

static void on_data(MqttSlot *s, esp_mqtt_event_handle_t e)
{
    if (e->current_data_offset == 0) {
        free_msg(s->partial);
        s->partial.topic = (char *)malloc(e->topic_len + 1);
        s->partial.data = (uint8_t *)malloc(e->total_data_len ? e->total_data_len : 1);
        if (!s->partial.topic || !s->partial.data) {
            free_msg(s->partial);
            return;
        }
        memcpy(s->partial.topic, e->topic, e->topic_len);
        s->partial.topic[e->topic_len] = '\0';
        s->partial_total = e->total_data_len;
    }
    if (!s->partial.data || e->current_data_offset + e->data_len > (int)s->partial_total) {
        return;
    }
    memcpy(s->partial.data + e->current_data_offset, e->data, e->data_len);
    if (e->current_data_offset + e->data_len == (int)s->partial_total) {
        s->partial.len = s->partial_total;
        if (xQueueSend(s->messages, &s->partial, 0) == pdTRUE) {
            s->partial = {};    // the queue owns it now
        } else {
            free_msg(s->partial);   // sketch is not calling loop(): drop
        }
    }
}

static void mqtt_event(void *arg, esp_event_base_t, int32_t id, void *data)
{
    MqttSlot *s = (MqttSlot *)arg;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s->state = BL_MQTT_CONNECTED;
        xEventGroupSetBits(s->events, BIT_CONNECTED);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s->state == BL_MQTT_CONNECTED) {
            s->state = BL_MQTT_CONNECTION_LOST;
        } else if (s->state < 0) {
            s->state = BL_MQTT_CONNECT_FAILED;
        }
        xEventGroupSetBits(s->events, BIT_DONE);
        break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            s->state = (int)e->error_handle->connect_return_code;     // 1..5, same as PubSubClient
        }
        xEventGroupSetBits(s->events, BIT_DONE);
        break;
    case MQTT_EVENT_DATA:
        on_data(s, e);
        break;
    default:
        break;
    }
}

static void stop_client(MqttSlot *s)
{
    if (s->client) {
        esp_mqtt_client_stop(s->client);
        esp_mqtt_client_destroy(s->client);
        s->client = NULL;
    }
}

extern "C" int bl_impl_mqtt_new(void)
{
    for (int h = 0; h < MAX_MQTT; h++) {
        if (!s_mqtt[h].used) {
            MqttSlot &s = s_mqtt[h];
            s = MqttSlot();
            s.messages = xQueueCreate(MSG_QUEUE_LEN, sizeof(MqttMsg));
            s.events = xEventGroupCreate();
            if (!s.messages || !s.events) {
                return BL_ERR_NO_MEM;
            }
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
    stop_client(s);
    MqttMsg m;
    while (xQueueReceive(s->messages, &m, 0) == pdTRUE) {
        free_msg(m);
    }
    free_msg(s->partial);
    vQueueDelete(s->messages);
    vEventGroupDelete(s->events);
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
    case BL_MQTT_BUFFER_SIZE:   s->buffer_size = value > 0 ? value : 1024; break;
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
    stop_client(s);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = s->host.c_str();
    cfg.broker.address.port = s->port;
    cfg.broker.address.transport = s->tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    if (s->tls) {
        if (s->ca_cert.length()) {
            cfg.broker.verification.certificate = s->ca_cert.c_str();
        } else {
            cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
    }
    if (s->client_id.length()) cfg.credentials.client_id = s->client_id.c_str();
    if (s->username.length()) cfg.credentials.username = s->username.c_str();
    if (s->password.length()) cfg.credentials.authentication.password = s->password.c_str();
    if (s->will_topic.length()) {
        cfg.session.last_will.topic = s->will_topic.c_str();
        cfg.session.last_will.msg = s->will_message.c_str();
        cfg.session.last_will.msg_len = s->will_message.length();
        cfg.session.last_will.qos = s->will_qos;
        cfg.session.last_will.retain = s->will_retain;
    }
    cfg.session.keepalive = s->keepalive;
    cfg.session.disable_clean_session = !s->clean_session;
    cfg.network.disable_auto_reconnect = true;
    cfg.buffer.size = s->buffer_size;

    s->client = esp_mqtt_client_init(&cfg);
    if (!s->client) {
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }
    esp_mqtt_client_register_event(s->client, MQTT_EVENT_ANY, mqtt_event, s);
    s->state = BL_MQTT_DISCONNECTED;
    xEventGroupClearBits(s->events, BIT_CONNECTED | BIT_DONE);
    if (esp_mqtt_client_start(s->client) != ESP_OK) {
        stop_client(s);
        s->state = BL_MQTT_CONNECT_FAILED;
        return 0;
    }
    EventBits_t bits;
    {
        WdtPause wdt;
        bits = xEventGroupWaitBits(s->events, BIT_CONNECTED | BIT_DONE, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(timeout_ms ? timeout_ms : 15000));
    }
    if (bits & BIT_CONNECTED) {
        return 1;
    }
    if (s->state == BL_MQTT_DISCONNECTED) {
        s->state = (bits & BIT_DONE) ? BL_MQTT_CONNECT_FAILED : BL_MQTT_CONNECTION_TIMEOUT;
    }
    stop_client(s);
    return 0;
}

extern "C" void bl_impl_mqtt_disconnect(int h)
{
    MqttSlot *s = slot_of(h);
    if (s) {
        stop_client(s);
        s->state = BL_MQTT_DISCONNECTED;
    }
}

extern "C" int bl_impl_mqtt_state(int h)
{
    MqttSlot *s = slot_of(h);
    return s ? s->state : BL_MQTT_DISCONNECTED;
}

extern "C" int bl_impl_mqtt_publish(int h, const char *topic, const uint8_t *payload, size_t len, int qos, int retain)
{
    MqttSlot *s = slot_of(h);
    if (!s || !s->client || s->state != BL_MQTT_CONNECTED || !topic) {
        return 0;
    }
    return esp_mqtt_client_publish(s->client, topic, (const char *)payload, (int)len, qos, retain) >= 0;
}

extern "C" int bl_impl_mqtt_subscribe(int h, const char *topic, int qos)
{
    MqttSlot *s = slot_of(h);
    if (!s || !s->client || s->state != BL_MQTT_CONNECTED || !topic) {
        return 0;
    }
    return esp_mqtt_client_subscribe_single(s->client, topic, qos) >= 0;
}

extern "C" int bl_impl_mqtt_unsubscribe(int h, const char *topic)
{
    MqttSlot *s = slot_of(h);
    if (!s || !s->client || s->state != BL_MQTT_CONNECTED || !topic) {
        return 0;
    }
    return esp_mqtt_client_unsubscribe(s->client, topic) >= 0;
}

extern "C" int bl_impl_mqtt_take(int h, char *topic, size_t topic_size, uint8_t *payload, size_t payload_size)
{
    MqttSlot *s = slot_of(h);
    MqttMsg m;
    if (!s || xQueueReceive(s->messages, &m, 0) != pdTRUE) {
        return -1;
    }
    if (topic && topic_size) {
        strlcpy(topic, m.topic, topic_size);
    }
    if (payload && payload_size) {
        memcpy(payload, m.data, m.len < payload_size ? m.len : payload_size);
    }
    int len = (int)m.len;
    free_msg(m);
    return len;
}
