/* API table: ESP-NOW. Received packets are queued by the WiFi task and the
 * sketch polls them, so no sketch code runs in a callback. */
#include <Arduino.h>
#include <WiFi.h>
#include "esp_now.h"
#include "freertos/queue.h"
#include "gen/bl_impl.h"

#define RX_QUEUE_LEN    8

struct espnow_packet {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

struct espnow_report {
    uint8_t mac[6];
    uint8_t delivered;
};

static QueueHandle_t s_queue;
static QueueHandle_t s_reports;     // delivery reports for the sketch's send callback
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    espnow_report r;
    memcpy(r.mac, info->des_addr, 6);
    r.delivered = status == ESP_NOW_SEND_SUCCESS;
    if (xQueueSend(s_reports, &r, 0) != pdTRUE) {
        espnow_report oldest;   // keep the newest reports
        xQueueReceive(s_reports, &oldest, 0);
        xQueueSend(s_reports, &r, 0);
    }
}

static void on_receive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    espnow_packet p;
    memcpy(p.mac, info->src_addr, 6);
    p.len = len > ESP_NOW_MAX_DATA_LEN ? ESP_NOW_MAX_DATA_LEN : len;
    memcpy(p.data, data, p.len);
    xQueueSend(s_queue, &p, 0);     // drop when the sketch falls behind
}

extern "C" int bl_impl_espnow_begin(void)
{
    if (s_queue) {
        return BL_OK;
    }
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    }
    s_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(espnow_packet));
    s_reports = xQueueCreate(RX_QUEUE_LEN, sizeof(espnow_report));
    if (!s_queue || !s_reports) {
        return BL_ERR_NO_MEM;
    }
    if (esp_now_init() != ESP_OK || esp_now_register_recv_cb(on_receive) != ESP_OK ||
        esp_now_register_send_cb(on_sent) != ESP_OK) {
        vQueueDelete(s_queue);
        vQueueDelete(s_reports);
        s_queue = s_reports = NULL;
        return BL_ERR_FAIL;
    }
    return BL_OK;
}

extern "C" int bl_impl_espnow_send_status(uint8_t *mac)
{
    espnow_report r;
    if (!s_reports || xQueueReceive(s_reports, &r, 0) != pdTRUE) {
        return -1;
    }
    if (mac) {
        memcpy(mac, r.mac, 6);
    }
    return r.delivered;
}

extern "C" int bl_impl_espnow_send(const uint8_t *mac, const uint8_t *data, size_t len)
{
    if (!s_queue) {
        return BL_ERR_STATE;
    }
    if (!data || !len || len > ESP_NOW_MAX_DATA_LEN) {
        return BL_ERR_ARG;
    }
    if (!mac) {
        mac = BROADCAST;
    }
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;           // current channel
        peer.ifidx = WIFI_IF_STA;
        if (esp_now_add_peer(&peer) != ESP_OK) {
            return BL_ERR_FAIL;
        }
    }
    return esp_now_send(mac, data, len) == ESP_OK ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_espnow_receive(uint8_t *mac, uint8_t *data, size_t size)
{
    espnow_packet p;
    if (!s_queue || !data || xQueueReceive(s_queue, &p, 0) != pdTRUE) {
        return 0;
    }
    if (mac) {
        memcpy(mac, p.mac, 6);
    }
    size_t n = p.len < size ? p.len : size;
    memcpy(data, p.data, n);
    return (int)n;
}
