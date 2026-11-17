/* API table: BLE (NimBLE through Arduino's BLE library).
 *   - BLE serial: a ready-made Nordic UART Service (ABI 1.1)
 *   - GATT server, advertising, scanning, client (ABI 1.3), by handle
 * BLE callbacks run on the NimBLE host task; they only queue bl_ble_event_t
 * records, which the SDK's pump delivers to the sketch on the sketch task. */
#include <Arduino.h>
#include "soc/soc_caps.h"

#include "bl_radio.h"
#if !BL_RADIO_BLE
/* no Bluetooth (an ESP32-P4 without a co-processor): every call reports BL_ERR_UNSUPPORTED or "nothing there". */
#include "gen/bl_impl.h"
#define BL_STUB_BLE 1
#include "gen/bl_impl_stubs.inc"
#else
#include "bl_hosted.h"

#include "esp_heap_caps.h"
#include <atomic>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_str.h"
#include "sketch_loader.h"

#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // central writes here
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // we notify here
#define RX_BUFFER   1024
#define TX_CHUNK    20      // fits the default 23-byte ATT MTU

#define EVENT_QUEUE_LEN 16
#define MAX_SERVICES    12
#define MAX_CHARS       32
#define MAX_RESULTS     40
#define MAX_CLIENTS     4
#define MAX_REMOTE      16

/* ---- shared state ---- */
static bool s_inited;
static BLEServer *s_server;
static QueueHandle_t s_events;
static std::atomic<int> s_connected;
static BLEService *s_services[MAX_SERVICES];
static BLECharacteristic *s_chars[MAX_CHARS];

static void push_event(const bl_ble_event_t &ev)
{
    if (s_events && xQueueSend(s_events, &ev, 0) != pdTRUE) {
        bl_ble_event_t dropped;     // full: keep the newest events
        xQueueReceive(s_events, &dropped, 0);
        xQueueSend(s_events, &ev, 0);
    }
}

static void push_simple(uint8_t type, int handle, uint16_t conn_id = 0, int32_t index = 0)
{
    bl_ble_event_t ev = {};
    ev.type = type;
    ev.handle = (int16_t)handle;
    ev.conn_id = conn_id;
    ev.index = index;
    push_event(ev);
}

static void push_data(uint8_t type, int handle, const uint8_t *data, size_t len, uint8_t flag = 0)
{
    bl_ble_event_t ev = {};
    ev.type = type;
    ev.handle = (int16_t)handle;
    ev.flag = flag;
    ev.len = (uint16_t)(len < sizeof(ev.data) ? len : sizeof(ev.data));
    if (data && ev.len) {
        memcpy(ev.data, data, ev.len);
    }
    push_event(ev);
}

/* Start the BLE stack once. Returns false if it could not start: typically
 * not enough internal RAM, e.g. on a board without PSRAM after WiFi has taken
 * its share. Every API entry checks this and returns BL_ERR_NO_MEM, so a
 * sketch gets an error instead of a crash. A later call tries again. */
static bool ensure_init(const char *name)
{
    if (s_inited) {
        return true;
    }
    if (!bl_hosted_ready()) {
        return false;           // the BLE co-processor doesn't answer
    }
    if (!s_events) {
        s_events = xQueueCreate(EVENT_QUEUE_LEN, sizeof(bl_ble_event_t));
    }
#if CONFIG_BTDM_CTRL_MODE_BTDM
    // Classic ESP32: if Bluetooth Classic (SPP) has not started, give its
    // controller memory (~30 KB of internal RAM) back to the heap and run the
    // controller BLE-only. Without PSRAM, Bluedroid does not fit next to WiFi
    // otherwise. SPP then reports a failure until the next restart.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE && !btMemReleased(BT_MODE_CLASSIC_BT)) {
        btMemRelease(BT_MODE_CLASSIC_BT);
    }
#endif
    if (!s_events || !BLEDevice::init(name && *name ? name : "Baseline") || !BLEDevice::getInitialized()) {
        BLOG_E("BLE could not start (largest free internal block %u bytes)",
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    s_inited = true;
    return true;
}

/* ---- BLE serial (NUS) state ---- */
static BLECharacteristic *s_nus_tx;
static StreamBufferHandle_t s_nus_rx;

class ServerCallbacks : public BLEServerCallbacks {
#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer *server, ble_gap_conn_desc *desc) override
    {
        s_connected++;
        push_simple(BL_BLE_EV_CONNECT, 0, desc ? desc->conn_handle : 0);
    }
    void onDisconnect(BLEServer *server, ble_gap_conn_desc *desc) override
    {
        if (s_connected > 0) {
            s_connected--;
        }
        push_simple(BL_BLE_EV_DISCONNECT, 0, desc ? desc->conn_handle : 0);
        if (s_nus_tx) {
            server->startAdvertising();     // BLE serial: let the next central find us
        }
    }
#else
    void onConnect(BLEServer *server) override
    {
        s_connected++;
        push_simple(BL_BLE_EV_CONNECT, 0);
    }
    void onDisconnect(BLEServer *server) override
    {
        if (s_connected > 0) {
            s_connected--;
        }
        push_simple(BL_BLE_EV_DISCONNECT, 0);
        if (s_nus_tx) {
            server->startAdvertising();
        }
    }
#endif
};
static ServerCallbacks s_server_cb;

static BLEServer *ensure_server()
{
    if (!ensure_init(NULL)) {
        return NULL;
    }
    if (!s_server) {
        s_server = BLEDevice::createServer();
        s_server->setCallbacks(&s_server_cb);
    }
    return s_server;
}

/* ---- BLE serial ---- */

class NusRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        xStreamBufferSend(s_nus_rx, c->getData(), c->getLength(), 0);
    }
};

extern "C" int bl_impl_ble_serial_begin(const char *name)
{
    if (s_nus_tx) {
        return BL_ERR_STATE;
    }
    s_nus_rx = xStreamBufferCreate(RX_BUFFER, 1);
    if (!s_nus_rx) {
        return BL_ERR_NO_MEM;
    }
    BLEServer *server = ensure_init(name) ? ensure_server() : NULL;
    if (!server) {
        vStreamBufferDelete(s_nus_rx);
        s_nus_rx = NULL;
        return BL_ERR_NO_MEM;
    }
    BLEService *svc = server->createService(NUS_SERVICE);
    s_nus_tx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
    s_nus_tx->addDescriptor(new BLE2902());     // NimBLE adds the CCCD itself
#endif
    BLECharacteristic *rx = svc->createCharacteristic(
        NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new NusRxCallbacks());
    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    return BL_OK;
}

extern "C" int bl_impl_ble_serial_connected(void)
{
    return s_connected > 0 ? 1 : 0;
}

extern "C" int bl_impl_ble_serial_write(const uint8_t *data, size_t len)
{
    if (!s_nus_tx || s_connected <= 0 || !data) {
        return 0;
    }
    for (size_t off = 0; off < len; off += TX_CHUNK) {
        size_t n = len - off < TX_CHUNK ? len - off : TX_CHUNK;
        s_nus_tx->setValue(data + off, n);
        s_nus_tx->notify();
    }
    return (int)len;
}

extern "C" int bl_impl_ble_serial_available(void)
{
    return s_nus_rx ? (int)xStreamBufferBytesAvailable(s_nus_rx) : 0;
}

extern "C" int bl_impl_ble_serial_read(void)
{
    uint8_t c;
    return (s_nus_rx && xStreamBufferReceive(s_nus_rx, &c, 1, 0) == 1) ? c : -1;
}

/* ---- device ---- */

extern "C" int bl_impl_ble_init(const char *name)
{
    if (!ensure_init(name)) {
        return BL_ERR_NO_MEM;
    }
    return BL_OK;
}

extern "C" void bl_impl_ble_deinit(void)
{
    if (!s_inited) {
        return;
    }
    BLEDevice::deinit(false);
    s_inited = false;
    s_server = NULL;
    s_nus_tx = NULL;
    s_connected = 0;
    memset(s_services, 0, sizeof(s_services));
    memset(s_chars, 0, sizeof(s_chars));
}

extern "C" int bl_impl_ble_address(char *buf, size_t size)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    String a = BLEDevice::getAddress().toString();
    if (buf && size) {
        bl_copy_str(buf, size, a);
    }
    return (int)a.length();
}

extern "C" int bl_impl_ble_set_mtu(uint16_t mtu)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    return BLEDevice::setMTU(mtu) == ESP_OK ? BL_OK : BL_ERR_FAIL;
}

/* ---- GATT server ---- */

extern "C" int bl_impl_ble_server_create(void)
{
    return ensure_server() ? 0 : BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_server_connected_count(void)
{
    return s_connected;
}

extern "C" void bl_impl_ble_server_disconnect(uint16_t conn_id)
{
    if (s_server) {
        s_server->disconnect(conn_id);
    }
}

extern "C" int bl_impl_ble_service_create(const char *uuid, int num_handles)
{
    if (!uuid) {
        return BL_ERR_ARG;
    }
    BLEServer *server = ensure_server();
    if (!server) {
        return BL_ERR_NO_MEM;
    }
    for (int h = 0; h < MAX_SERVICES; h++) {
        if (!s_services[h]) {
            s_services[h] = server->createService(BLEUUID(uuid), num_handles > 0 ? num_handles : 15, 0);
            return s_services[h] ? h : BL_ERR_FAIL;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_service_start(int svc)
{
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc]) {
        return BL_ERR_ARG;
    }
    s_services[svc]->start();
    return BL_OK;
}

static int char_handle(BLECharacteristic *c)
{
    for (int h = 0; h < MAX_CHARS; h++) {
        if (s_chars[h] == c) {
            return h;
        }
    }
    return -1;
}

class CharCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        push_data(BL_BLE_EV_WRITE, char_handle(c), c->getData(), c->getLength());
    }
    void onRead(BLECharacteristic *c) override
    {
        push_simple(BL_BLE_EV_READ, char_handle(c));
    }
};
static CharCallbacks s_char_cb;

static uint32_t to_lib_props(uint32_t p)
{
    uint32_t r = 0;
    if (p & BL_BLE_PROP_READ) r |= BLECharacteristic::PROPERTY_READ;
    if (p & BL_BLE_PROP_WRITE) r |= BLECharacteristic::PROPERTY_WRITE;
    if (p & BL_BLE_PROP_NOTIFY) r |= BLECharacteristic::PROPERTY_NOTIFY;
    if (p & BL_BLE_PROP_INDICATE) r |= BLECharacteristic::PROPERTY_INDICATE;
    if (p & BL_BLE_PROP_WRITE_NR) r |= BLECharacteristic::PROPERTY_WRITE_NR;
    if (p & BL_BLE_PROP_BROADCAST) r |= BLECharacteristic::PROPERTY_BROADCAST;
    return r;
}

extern "C" int bl_impl_ble_char_create(int svc, const char *uuid, uint32_t properties)
{
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc] || !uuid) {
        return BL_ERR_ARG;
    }
    for (int h = 0; h < MAX_CHARS; h++) {
        if (!s_chars[h]) {
            BLECharacteristic *c = s_services[svc]->createCharacteristic(BLEUUID(uuid), to_lib_props(properties));
            if (!c) {
                return BL_ERR_FAIL;
            }
#if defined(CONFIG_BLUEDROID_ENABLED)
            if (properties & (BL_BLE_PROP_NOTIFY | BL_BLE_PROP_INDICATE)) {
                c->addDescriptor(new BLE2902());
            }
#endif
            c->setCallbacks(&s_char_cb);
            s_chars[h] = c;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

static BLECharacteristic *char_of(int h)
{
    return (h >= 0 && h < MAX_CHARS) ? s_chars[h] : NULL;
}

extern "C" int bl_impl_ble_char_set_value(int ch, const uint8_t *data, size_t len)
{
    BLECharacteristic *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    c->setValue(data, len);
    return BL_OK;
}

extern "C" int bl_impl_ble_char_get_value(int ch, uint8_t *buf, size_t size)
{
    BLECharacteristic *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    size_t len = c->getLength();
    if (buf && size) {
        memcpy(buf, c->getData(), len < size ? len : size);
    }
    return (int)len;
}

extern "C" int bl_impl_ble_char_notify(int ch, int indicate)
{
    BLECharacteristic *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    if (indicate) {
        c->indicate();
    } else {
        c->notify();
    }
    return BL_OK;
}

extern "C" int bl_impl_ble_char_add_descriptor(int ch, const char *uuid, const uint8_t *value, size_t len)
{
    BLECharacteristic *c = char_of(ch);
    if (!c || !uuid) {
        return BL_ERR_ARG;
    }
    BLEUUID id(uuid);
    if (id.equals(BLEUUID((uint16_t)0x2902))) {
#if defined(CONFIG_BLUEDROID_ENABLED)
        c->addDescriptor(new BLE2902());
#endif
        return BL_OK;     // NimBLE manages the CCCD itself
    }
    BLEDescriptor *d = new BLEDescriptor(id, len > 20 ? len : 20);
    if (value && len) {
        d->setValue(value, len);
    }
    c->addDescriptor(d);
    return BL_OK;
}

/* ---- advertising ---- */

extern "C" int bl_impl_ble_adv_config(int what, const char *text, const uint8_t *data, size_t value)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    switch (what) {
    case BL_BLE_ADV_SERVICE_UUID:
        if (!text) return BL_ERR_ARG;
        adv->addServiceUUID(text);
        return BL_OK;
    case BL_BLE_ADV_SCAN_RESPONSE: adv->setScanResponse(value != 0); return BL_OK;
    case BL_BLE_ADV_NAME: {
        if (!text) return BL_ERR_ARG;
#if CONFIG_BT_NIMBLE_ENABLED
        adv->setName(text);
#else   // Bluedroid (classic ESP32): the name goes in the advertisement data
        BLEAdvertisementData ad;
        ad.setName(text);
        adv->setAdvertisementData(ad);
#endif
        return BL_OK;
    }
    case BL_BLE_ADV_MANUFACTURER: {
        if (!data) return BL_ERR_ARG;
        BLEAdvertisementData ad;
        ad.setManufacturerData(String((const char *)data, value));
        adv->setAdvertisementData(ad);
        return BL_OK;
    }
    case BL_BLE_ADV_APPEARANCE:    adv->setAppearance((uint16_t)value); return BL_OK;
    case BL_BLE_ADV_MIN_PREFERRED: adv->setMinPreferred((uint16_t)value); return BL_OK;
    case BL_BLE_ADV_MAX_PREFERRED: adv->setMaxPreferred((uint16_t)value); return BL_OK;
    case BL_BLE_ADV_MIN_INTERVAL:  adv->setMinInterval((uint16_t)value); return BL_OK;
    case BL_BLE_ADV_MAX_INTERVAL:  adv->setMaxInterval((uint16_t)value); return BL_OK;
    case BL_BLE_ADV_RESET:         adv->reset(); return BL_OK;
    default:                       return BL_ERR_ARG;
    }
}

extern "C" int bl_impl_ble_adv_start(void)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    BLEDevice::startAdvertising();
    return BL_OK;
}

extern "C" int bl_impl_ble_adv_stop(void)
{
    if (!s_inited) {
        return BL_ERR_STATE;
    }
    BLEDevice::stopAdvertising();
    return BL_OK;
}

/* ---- scanning ---- */

static bl_ble_adv_t s_results[MAX_RESULTS];
static int s_result_count;              // guarded by s_results_lock
static volatile bool s_scanning;
static portMUX_TYPE s_results_lock = portMUX_INITIALIZER_UNLOCKED;

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override
    {
        bl_ble_adv_t r = {};
        bl_copy_str(r.address, sizeof(r.address), dev.getAddress().toString());
        r.address_type = dev.getAddressType();
        r.rssi = (int16_t)dev.getRSSI();
        if (dev.haveName()) {
            r.has_name = 1;
            bl_copy_str(r.name, sizeof(r.name), dev.getName());
        }
        if (dev.haveServiceUUID()) {
            r.has_service_uuid = 1;
            bl_copy_str(r.service_uuid, sizeof(r.service_uuid), dev.getServiceUUID().toString());
        }
        if (dev.haveTXPower()) {
            r.has_tx_power = 1;
            r.tx_power = dev.getTXPower();
        }
        if (dev.haveAppearance()) {
            r.appearance = dev.getAppearance();
        }
        if (dev.haveManufacturerData()) {
            String m = dev.getManufacturerData();
            r.has_manufacturer_data = 1;
            r.manufacturer_len = (uint8_t)(m.length() < sizeof(r.manufacturer_data) ? m.length() : sizeof(r.manufacturer_data));
            memcpy(r.manufacturer_data, m.c_str(), r.manufacturer_len);
        }
        int index = -1;
        bool is_new = false;
        portENTER_CRITICAL(&s_results_lock);
        for (int i = 0; i < s_result_count; i++) {
            if (strcmp(s_results[i].address, r.address) == 0) {
                index = i;      // seen before: refresh
                break;
            }
        }
        if (index < 0 && s_result_count < MAX_RESULTS) {
            index = s_result_count++;
            is_new = true;
        }
        if (index >= 0) {
            s_results[index] = r;
        }
        portEXIT_CRITICAL(&s_results_lock);
        if (is_new) {
            push_simple(BL_BLE_EV_SCAN_RESULT, -1, 0, index);   // once per device, as in Arduino
        }
    }
};
static ScanCallbacks s_scan_cb;

static void scan_complete(BLEScanResults)
{
    s_scanning = false;
    push_simple(BL_BLE_EV_SCAN_DONE, -1);
}

extern "C" int bl_impl_ble_scan_start(uint32_t seconds, int active, uint16_t interval_ms, uint16_t window_ms)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    BLEScan *scan = BLEDevice::getScan();
    // Duplicate reports, by stack (Baseline de-duplicates in s_results either way):
    //  - Bluedroid (classic ESP32): want them. Otherwise the library keeps its
    //    own copy of every device it hears for the whole scan, which ran the
    //    classic ESP32 out of heap in a busy room.
    //  - NimBLE (S3, C3, C5, C6): do not. With duplicates on, the library
    //    appends every new report to the device's payload, so each device's
    //    copy grows by kilobytes during a scan; the C3 ran out of heap and the
    //    library aborted.
#if CONFIG_BT_NIMBLE_ENABLED
    const bool want_duplicates = false;
#else
    const bool want_duplicates = true;
#endif
    scan->setAdvertisedDeviceCallbacks(&s_scan_cb, want_duplicates);
    scan->setActiveScan(active != 0);
    if (interval_ms) {
        scan->setInterval(interval_ms);
    }
    if (window_ms) {
        scan->setWindow(window_ms);
    }
    portENTER_CRITICAL(&s_results_lock);
    s_result_count = 0;
    portEXIT_CRITICAL(&s_results_lock);
    scan->clearResults();
    s_scanning = true;
    if (!scan->start(seconds ? seconds : 5, scan_complete, false)) {
        s_scanning = false;
        return BL_ERR_FAIL;
    }
    return BL_OK;
}

extern "C" int bl_impl_ble_scan_stop(void)
{
    if (s_inited) {
        BLEDevice::getScan()->stop();
    }
    s_scanning = false;
    return BL_OK;
}

extern "C" int bl_impl_ble_scan_running(void)
{
    return s_scanning ? 1 : 0;
}

extern "C" int bl_impl_ble_scan_count(void)
{
    portENTER_CRITICAL(&s_results_lock);
    int n = s_result_count;
    portEXIT_CRITICAL(&s_results_lock);
    return n;
}

extern "C" int bl_impl_ble_scan_result(int index, bl_ble_adv_t *dev)
{
    if (!dev || index < 0) {
        return BL_ERR_ARG;
    }
    portENTER_CRITICAL(&s_results_lock);
    bool ok = index < s_result_count;
    if (ok) {
        *dev = s_results[index];
    }
    portEXIT_CRITICAL(&s_results_lock);
    return ok ? BL_OK : BL_ERR_ARG;
}

extern "C" void bl_impl_ble_scan_clear(void)
{
    portENTER_CRITICAL(&s_results_lock);
    s_result_count = 0;
    portEXIT_CRITICAL(&s_results_lock);
    if (s_inited) {
        BLEDevice::getScan()->clearResults();
    }
}

/* ---- client ---- */

static BLEClient *s_clients[MAX_CLIENTS];
static BLERemoteCharacteristic *s_remote[MAX_REMOTE];

static int client_handle(BLEClient *c)
{
    for (int h = 0; h < MAX_CLIENTS; h++) {
        if (s_clients[h] == c) {
            return h;
        }
    }
    return -1;
}

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient *c) override { push_simple(BL_BLE_EV_CLIENT_CONNECT, client_handle(c)); }
    void onDisconnect(BLEClient *c) override { push_simple(BL_BLE_EV_CLIENT_DISCONNECT, client_handle(c)); }
};
static ClientCallbacks s_client_cb;

static BLEClient *client_of(int h)
{
    return (h >= 0 && h < MAX_CLIENTS) ? s_clients[h] : NULL;
}

extern "C" int bl_impl_ble_client_connect(const char *address, int addr_type)
{
    if (!address) {
        return BL_ERR_ARG;
    }
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    int h = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!s_clients[i]) {
            h = i;
            break;
        }
    }
    if (h < 0) {
        return BL_ERR_NO_MEM;
    }
    BLEClient *c = BLEDevice::createClient();
    c->setClientCallbacks(&s_client_cb);
    s_clients[h] = c;
    WdtPause wdt;
    if (!c->connect(BLEAddress(String(address), addr_type >= 0 ? addr_type : 0), addr_type >= 0 ? addr_type : 0, 5000)) {
        s_clients[h] = NULL;
        delete c;
        return BL_ERR_FAIL;
    }
    return h;
}

extern "C" void bl_impl_ble_client_disconnect(int cl)
{
    BLEClient *c = client_of(cl);
    if (c) {
        c->disconnect();
    }
}

extern "C" int bl_impl_ble_client_connected(int cl)
{
    BLEClient *c = client_of(cl);
    return (c && c->isConnected()) ? 1 : 0;
}

extern "C" int bl_impl_ble_client_rssi(int cl)
{
    BLEClient *c = client_of(cl);
    return c ? c->getRssi() : 0;
}

extern "C" int bl_impl_ble_remote_char(int cl, const char *service_uuid, const char *char_uuid)
{
    BLEClient *c = client_of(cl);
    if (!c || !service_uuid || !char_uuid) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;   // service discovery talks to the peripheral
    BLERemoteService *svc = c->getService(BLEUUID(service_uuid));
    BLERemoteCharacteristic *rc = svc ? svc->getCharacteristic(BLEUUID(char_uuid)) : NULL;
    if (!rc) {
        return BL_ERR_FAIL;
    }
    for (int h = 0; h < MAX_REMOTE; h++) {
        if (s_remote[h] == rc) {
            return h;
        }
    }
    for (int h = 0; h < MAX_REMOTE; h++) {
        if (!s_remote[h]) {
            s_remote[h] = rc;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

static BLERemoteCharacteristic *remote_of(int h)
{
    return (h >= 0 && h < MAX_REMOTE) ? s_remote[h] : NULL;
}

extern "C" int bl_impl_ble_remote_read(int rc, uint8_t *buf, size_t size)
{
    BLERemoteCharacteristic *r = remote_of(rc);
    if (!r) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;
    String v = r->readValue();
    if (buf && size) {
        memcpy(buf, v.c_str(), v.length() < size ? v.length() : size);
    }
    return (int)v.length();
}

extern "C" int bl_impl_ble_remote_write(int rc, const uint8_t *data, size_t len, int response)
{
    BLERemoteCharacteristic *r = remote_of(rc);
    if (!r || !data) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;
    return r->writeValue((uint8_t *)data, len, response != 0) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_ble_remote_subscribe(int rc, int mode)
{
    BLERemoteCharacteristic *r = remote_of(rc);
    if (!r) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;
    if (mode == 0) {
        r->registerForNotify(nullptr);
        return BL_OK;
    }
    r->registerForNotify([rc](BLERemoteCharacteristic *, uint8_t *data, size_t len, bool is_notify) {
        push_data(BL_BLE_EV_NOTIFY, rc, data, len, is_notify ? 1 : 0);
    }, mode == 1);
    return BL_OK;
}

extern "C" uint32_t bl_impl_ble_remote_props(int rc)
{
    BLERemoteCharacteristic *r = remote_of(rc);
    if (!r) {
        return 0;
    }
    uint32_t p = 0;
    if (r->canRead()) p |= BL_BLE_PROP_READ;
    if (r->canWrite()) p |= BL_BLE_PROP_WRITE;
    if (r->canNotify()) p |= BL_BLE_PROP_NOTIFY;
    if (r->canIndicate()) p |= BL_BLE_PROP_INDICATE;
    if (r->canWriteNoResponse()) p |= BL_BLE_PROP_WRITE_NR;
    if (r->canBroadcast()) p |= BL_BLE_PROP_BROADCAST;
    return p;
}

/* ---- events ---- */

extern "C" int bl_impl_ble_event_take(bl_ble_event_t *ev)
{
    return (ev && s_events && xQueueReceive(s_events, ev, 0) == pdTRUE) ? 1 : 0;
}

#endif /* BL_RADIO_BLE */
