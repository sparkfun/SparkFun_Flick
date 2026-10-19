/*
 * baseline_types.h - Constants and types shared by Baseline and sketches.
 * Values here are part of the ABI: never change an existing value.
 */
#ifndef BASELINE_TYPES_H
#define BASELINE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

/* ---- Errors (all negative) ---- */
#define BL_OK                0
#define BL_ERR_FAIL         -1
#define BL_ERR_ARG          -2      /* bad argument, e.g. invalid pin */
#define BL_ERR_NO_MEM       -3
#define BL_ERR_STATE        -4
#define BL_ERR_UNSUPPORTED  -5
#define BL_ERR_NACK         -6      /* I2C: no acknowledge */
#define BL_ERR_TIMEOUT      -7

/* ---- WiFi status (same values as Arduino's wl_status_t) ---- */
#define BL_WIFI_IDLE            0
#define BL_WIFI_NO_SSID         1
#define BL_WIFI_SCAN_COMPLETED  2
#define BL_WIFI_CONNECTED       3
#define BL_WIFI_CONNECT_FAILED  4
#define BL_WIFI_CONNECTION_LOST 5
#define BL_WIFI_DISCONNECTED    6
#define BL_WIFI_STOPPED         254

/* ---- GPIO ---- */
#define BL_LOW               0
#define BL_HIGH              1

#define BL_INPUT             0x01
#define BL_OUTPUT            0x03
#define BL_INPUT_PULLUP      0x05
#define BL_INPUT_PULLDOWN    0x09
#define BL_OUTPUT_OPEN_DRAIN 0x13

/* ---- bl_system_info() keys ---- */
#define BL_INFO_CPU_MHZ         1
#define BL_INFO_HEAP_SIZE       2
#define BL_INFO_MIN_FREE_HEAP   3
#define BL_INFO_MAX_ALLOC_HEAP  4
#define BL_INFO_PSRAM_SIZE      5
#define BL_INFO_FREE_PSRAM      6
#define BL_INFO_FLASH_SIZE      7

/* ---- Callbacks run synchronously on the sketch task ---- */
typedef void (*bl_callback_t)(void *arg);

/* ---- GPIO interrupt modes (same values as Arduino-ESP32) ---- */
#define BL_RISING   0x01
#define BL_FALLING  0x02
#define BL_CHANGE   0x03
#define BL_ONLOW    0x04
#define BL_ONHIGH   0x05

/* ---- SPI ---- */
#define BL_LSBFIRST 0
#define BL_MSBFIRST 1

/* ---- WiFi modes and addresses ---- */
#define BL_WIFI_MODE_OFF    0
#define BL_WIFI_MODE_STA    1
#define BL_WIFI_MODE_AP     2
#define BL_WIFI_MODE_AP_STA 3

#define BL_IP_STA       0
#define BL_IP_GATEWAY   1
#define BL_IP_SUBNET    2
#define BL_IP_DNS       3
#define BL_IP_SOFTAP    4

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth;           /* 0 = open (same values as wifi_auth_mode_t) */
    uint8_t bssid[6];
} bl_wifi_ap_t;

/* ---- WebServer ---- */
#define BL_HTTP_DELETE  0   /* same values as Arduino's HTTPMethod */
#define BL_HTTP_GET     1
#define BL_HTTP_HEAD    2
#define BL_HTTP_POST    3
#define BL_HTTP_PUT     4
#define BL_HTTP_OPTIONS 6
#define BL_HTTP_PATCH   28
#define BL_HTTP_ANY     255

#define BL_WEB_URI          1   /* request path */
#define BL_WEB_METHOD       2   /* returns BL_HTTP_* (no text) */
#define BL_WEB_ARG          3   /* argument by name */
#define BL_WEB_ARG_AT       4   /* argument value by index */
#define BL_WEB_ARG_NAME_AT  5   /* argument name by index */
#define BL_WEB_ARG_COUNT    6   /* returns the count (no text) */
#define BL_WEB_HAS_ARG      7   /* returns 1/0 (no text) */
#define BL_WEB_HEADER       8   /* collected header by name */
#define BL_WEB_CLIENT_IP    9   /* returns the IPv4 address as a number (no text) */

/* ---- BLE ---- */
#define BL_BLE_PROP_READ        0x01
#define BL_BLE_PROP_WRITE       0x02
#define BL_BLE_PROP_NOTIFY      0x04
#define BL_BLE_PROP_INDICATE    0x08
#define BL_BLE_PROP_WRITE_NR    0x10
#define BL_BLE_PROP_BROADCAST   0x20

#define BL_BLE_ADV_SERVICE_UUID   1   /* text = UUID */
#define BL_BLE_ADV_SCAN_RESPONSE  2   /* value = 0/1 */
#define BL_BLE_ADV_NAME           3   /* text */
#define BL_BLE_ADV_MANUFACTURER   4   /* data, value = length */
#define BL_BLE_ADV_APPEARANCE     5   /* value */
#define BL_BLE_ADV_MIN_PREFERRED  6   /* value */
#define BL_BLE_ADV_MAX_PREFERRED  7   /* value */
#define BL_BLE_ADV_MIN_INTERVAL   8   /* value */
#define BL_BLE_ADV_MAX_INTERVAL   9   /* value */
#define BL_BLE_ADV_RESET          10

typedef struct {
    char address[18];
    char name[32];
    char service_uuid[37];
    int16_t rssi;
    int8_t tx_power;
    uint8_t address_type;
    uint8_t has_name, has_service_uuid, has_tx_power, has_manufacturer_data;
    uint16_t appearance;
    uint8_t manufacturer_len;
    uint8_t manufacturer_data[31];
} bl_ble_adv_t;

#define BL_BLE_EV_CONNECT       1   /* conn_id; handle = server */
#define BL_BLE_EV_DISCONNECT    2   /* conn_id; handle = server */
#define BL_BLE_EV_WRITE         3   /* handle = characteristic; data = value written */
#define BL_BLE_EV_READ          4   /* handle = characteristic (after the read was answered) */
#define BL_BLE_EV_NOTIFY        5   /* handle = remote characteristic; data; flag 1 = notification */
#define BL_BLE_EV_CLIENT_CONNECT    6   /* handle = client */
#define BL_BLE_EV_CLIENT_DISCONNECT 7   /* handle = client */
#define BL_BLE_EV_SCAN_RESULT   8   /* index = scan result index */
#define BL_BLE_EV_SCAN_DONE     9

typedef struct {
    uint8_t type;           /* BL_BLE_EV_* */
    uint8_t flag;
    uint16_t len;           /* bytes in data */
    int16_t handle;
    uint16_t conn_id;
    int32_t index;
    uint8_t data[244];
} bl_ble_event_t;

/* ---- Preferences ---- */
#define BL_PREF_I8      0
#define BL_PREF_U8      1
#define BL_PREF_I16     2
#define BL_PREF_U16     3
#define BL_PREF_I32     4
#define BL_PREF_U32     5
#define BL_PREF_I64     6
#define BL_PREF_U64     7
#define BL_PREF_STRING  8
#define BL_PREF_BYTES   9
#define BL_PREF_FLOAT   10
#define BL_PREF_DOUBLE  11
#define BL_PREF_BOOL    12

#define BL_PREF_INFO_EXISTS       1
#define BL_PREF_INFO_TYPE         2   /* Arduino PreferenceType: PT_I8 = 0 ... PT_BLOB = 9, PT_INVALID = 10 */
#define BL_PREF_INFO_LENGTH       3   /* bytes (blobs) or characters (strings) */
#define BL_PREF_INFO_FREE_ENTRIES 4

/* ---- LittleFS ---- */
#define BL_FS_TOTAL_BYTES   1
#define BL_FS_USED_BYTES    2

#define BL_FS_FILE_POSITION   1
#define BL_FS_FILE_SIZE       2
#define BL_FS_FILE_IS_DIR     3
#define BL_FS_FILE_LAST_WRITE 4

#define BL_FS_EXISTS  1
#define BL_FS_REMOVE  2
#define BL_FS_RENAME  3
#define BL_FS_MKDIR   4
#define BL_FS_RMDIR   5

/* ---- mDNS ---- */
#define BL_MDNS_HOSTNAME 1
#define BL_MDNS_IP       2
#define BL_MDNS_PORT     3

/* ---- MQTT ---- */
#define BL_MQTT_SERVER        1   /* text = host, value = port */
#define BL_MQTT_CLIENT_ID     2
#define BL_MQTT_USERNAME      3
#define BL_MQTT_PASSWORD      4
#define BL_MQTT_KEEPALIVE     5   /* value = seconds */
#define BL_MQTT_WILL_TOPIC    6
#define BL_MQTT_WILL_MESSAGE  7
#define BL_MQTT_WILL_QOS      8
#define BL_MQTT_WILL_RETAIN   9
#define BL_MQTT_CLEAN_SESSION 10  /* value = 0/1 */
#define BL_MQTT_TLS           11  /* value = 0 plain TCP, 1 TLS (built-in CA bundle unless BL_MQTT_CA_CERT) */
#define BL_MQTT_CA_CERT       12  /* text = PEM */
#define BL_MQTT_BUFFER_SIZE   13  /* value = bytes */

/* PubSubClient state() values */
#define BL_MQTT_CONNECTION_TIMEOUT     -4
#define BL_MQTT_CONNECTION_LOST        -3
#define BL_MQTT_CONNECT_FAILED         -2
#define BL_MQTT_DISCONNECTED           -1
#define BL_MQTT_CONNECTED               0
#define BL_MQTT_CONNECT_BAD_PROTOCOL    1
#define BL_MQTT_CONNECT_BAD_CLIENT_ID   2
#define BL_MQTT_CONNECT_UNAVAILABLE     3
#define BL_MQTT_CONNECT_BAD_CREDENTIALS 4
#define BL_MQTT_CONNECT_UNAUTHORIZED    5

/* ---- Logging ---- */
#define BL_LOG_ERROR         1
#define BL_LOG_WARN          2
#define BL_LOG_INFO          3
#define BL_LOG_DEBUG         4
#define BL_LOG_VERBOSE       5

#endif /* BASELINE_TYPES_H */
