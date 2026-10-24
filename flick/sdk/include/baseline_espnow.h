/*
 * baseline_espnow.h - the classic ESP-IDF ESP-NOW API for sketches.
 *
 *   esp_now_init();
 *   esp_now_register_recv_cb(onReceive);     // IDF 5 and IDF 4 callback forms
 *   esp_now_peer_info_t peer = {};  memcpy(peer.peer_addr, mac, 6);  esp_now_add_peer(&peer);
 *   esp_now_send(mac, data, len);
 *
 * Calls go to Baseline. Callbacks run on the sketch's task (from delay(),
 * yield(), or between loop() calls), not on the WiFi task, so they may use
 * Serial and sketch variables freely. Encryption (lmk/pmk) is not supported.
 */
#ifndef BASELINE_ESPNOW_H
#define BASELINE_ESPNOW_H

#ifdef __cplusplus

typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK                      0
#define ESP_FAIL                    -1
#define ESP_ERR_NO_MEM              0x101
#define ESP_ERR_INVALID_ARG         0x102
#define ESP_ERR_NOT_SUPPORTED       0x106
#endif
#define ESP_ERR_ESPNOW_BASE         0x3066
#define ESP_ERR_ESPNOW_NOT_INIT     (ESP_ERR_ESPNOW_BASE + 1)
#define ESP_ERR_ESPNOW_ARG          (ESP_ERR_ESPNOW_BASE + 2)
#define ESP_ERR_ESPNOW_NOT_FOUND    (ESP_ERR_ESPNOW_BASE + 5)

#define ESP_NOW_ETH_ALEN            6
#define ESP_NOW_KEY_LEN             16
#define ESP_NOW_MAX_DATA_LEN        250

typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;
typedef enum { ESP_NOW_SEND_SUCCESS = 0, ESP_NOW_SEND_FAIL } esp_now_send_status_t;

typedef struct esp_now_peer_info {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    uint8_t lmk[ESP_NOW_KEY_LEN];
    uint8_t channel;
    wifi_interface_t ifidx;
    bool encrypt;
    void *priv;
} esp_now_peer_info_t;

typedef struct esp_now_recv_info {
    uint8_t *src_addr;
    uint8_t *des_addr;
    void *rx_ctrl;          /* not provided (NULL) */
} esp_now_recv_info_t;

typedef struct {
    uint8_t *des_addr;
    uint8_t *src_addr;
    wifi_interface_t ifidx;
    uint8_t *data;          /* not provided (NULL) */
    uint8_t data_len;
    int rate;
    int tx_status;
} wifi_tx_info_t;
typedef wifi_tx_info_t esp_now_send_info_t;

/* IDF 5.x callback forms */
typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *info, const uint8_t *data, int len);
typedef void (*esp_now_send_cb_t)(const esp_now_send_info_t *info, esp_now_send_status_t status);
/* IDF 4.x / older Arduino-ESP32 callback forms */
typedef void (*esp_now_legacy_recv_cb_t)(const uint8_t *mac, const uint8_t *data, int len);
typedef void (*esp_now_legacy_send_cb_t)(const uint8_t *mac, esp_now_send_status_t status);

esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_register_recv_cb(esp_now_legacy_recv_cb_t cb);
esp_err_t esp_now_unregister_recv_cb(void);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_register_send_cb(esp_now_legacy_send_cb_t cb);
esp_err_t esp_now_unregister_send_cb(void);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_del_peer(const uint8_t *peer_addr);
bool esp_now_is_peer_exist(const uint8_t *peer_addr);
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);

#endif /* __cplusplus */
#endif /* BASELINE_ESPNOW_H */
