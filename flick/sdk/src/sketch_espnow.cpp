/* sketch_espnow.cpp - classic esp_now_* API on top of Baseline (see baseline_espnow.h). */
#include "baseline.h"

#if defined(BL_HAS_WIFI)     /* the whole file: boards with this feature only */

#define MAX_PEERS 20

static bool s_init;
static esp_now_recv_cb_t s_recv;
static esp_now_legacy_recv_cb_t s_recv_legacy;
static esp_now_send_cb_t s_send;
static esp_now_legacy_send_cb_t s_send_legacy;
static uint8_t s_peers[MAX_PEERS][6];
static int s_peer_count;

static int find_peer(const uint8_t *mac)
{
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i], mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

/* Runs on the sketch task: deliver queued packets and delivery reports. */
static void espnow_pump()
{
    static uint8_t data[ESP_NOW_MAX_DATA_LEN];
    uint8_t mac[6];
    static uint8_t self[6];
    if (s_recv || s_recv_legacy) {
        int len;
        while ((len = bl_espnow_receive(mac, data, sizeof(data))) > 0) {
            if (s_recv) {
                bl_wifi_mac(self);
                esp_now_recv_info_t info = { mac, self, 0 };
                s_recv(&info, data, len);
            } else {
                s_recv_legacy(mac, data, len);
            }
        }
    }
    if (s_send || s_send_legacy) {
        int delivered;
        while ((delivered = bl_espnow_send_status(mac)) >= 0) {
            esp_now_send_status_t st = delivered ? ESP_NOW_SEND_SUCCESS : ESP_NOW_SEND_FAIL;
            if (s_send) {
                bl_wifi_mac(self);
                wifi_tx_info_t info = {};
                info.des_addr = mac;
                info.src_addr = self;
                info.tx_status = st;
                s_send(&info, st);
            } else {
                s_send_legacy(mac, st);
            }
        }
    }
}

esp_err_t esp_now_init(void)
{
    if (bl_espnow_begin() != BL_OK) {
        return ESP_FAIL;
    }
    s_init = true;
    bl_sketch_add_pump(espnow_pump);
    return ESP_OK;
}

esp_err_t esp_now_deinit(void)
{
    s_recv = 0;
    s_recv_legacy = 0;
    s_send = 0;
    s_send_legacy = 0;
    return ESP_OK;
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb)
{
    s_recv = cb;
    s_recv_legacy = 0;
    return s_init ? ESP_OK : ESP_ERR_ESPNOW_NOT_INIT;
}

esp_err_t esp_now_register_recv_cb(esp_now_legacy_recv_cb_t cb)
{
    s_recv_legacy = cb;
    s_recv = 0;
    return s_init ? ESP_OK : ESP_ERR_ESPNOW_NOT_INIT;
}

esp_err_t esp_now_unregister_recv_cb(void)
{
    s_recv = 0;
    s_recv_legacy = 0;
    return ESP_OK;
}

esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb)
{
    s_send = cb;
    s_send_legacy = 0;
    return s_init ? ESP_OK : ESP_ERR_ESPNOW_NOT_INIT;
}

esp_err_t esp_now_register_send_cb(esp_now_legacy_send_cb_t cb)
{
    s_send_legacy = cb;
    s_send = 0;
    return s_init ? ESP_OK : ESP_ERR_ESPNOW_NOT_INIT;
}

esp_err_t esp_now_unregister_send_cb(void)
{
    s_send = 0;
    s_send_legacy = 0;
    return ESP_OK;
}

/* Baseline adds peers on first send; we only remember them for is_peer_exist(). */
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    if (!s_init) {
        return ESP_ERR_ESPNOW_NOT_INIT;
    }
    if (!peer) {
        return ESP_ERR_ESPNOW_ARG;
    }
    if (peer->encrypt) {
        log_e("ESP-NOW encryption is not supported by Baseline yet");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (find_peer(peer->peer_addr) < 0 && s_peer_count < MAX_PEERS) {
        memcpy(s_peers[s_peer_count++], peer->peer_addr, 6);
    }
    return ESP_OK;
}

esp_err_t esp_now_del_peer(const uint8_t *peer_addr)
{
    int i = peer_addr ? find_peer(peer_addr) : -1;
    if (i < 0) {
        return ESP_ERR_ESPNOW_NOT_FOUND;
    }
    memmove(s_peers[i], s_peers[i + 1], (s_peer_count - i - 1) * 6);
    s_peer_count--;
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *peer_addr)
{
    return peer_addr && find_peer(peer_addr) >= 0;
}

esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len)
{
    if (!s_init) {
        return ESP_ERR_ESPNOW_NOT_INIT;
    }
    if (!data || !len || len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_ESPNOW_ARG;
    }
    return bl_espnow_send(peer_addr, data, len) == BL_OK ? ESP_OK : ESP_FAIL;
}

#endif /* BL_HAS_WIFI */
