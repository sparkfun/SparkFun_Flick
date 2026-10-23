/* sketch_ble.cpp - Arduino BLE library classes on top of Baseline (see baseline_ble.h). */
#include "baseline.h"

#define MAX_CHARS   32      /* match Baseline's tables */
#define MAX_REMOTE  16
#define MAX_CLIENTS 4

static bool s_inited;
static uint16_t s_mtu = 23;
static BLEServer s_server;
static BLEAdvertising s_advertising;
static BLEScan s_scan;
static BLECharacteristic *s_chars[MAX_CHARS];
static BLERemoteCharacteristic *s_remote[MAX_REMOTE];
static BLEClient *s_clients[MAX_CLIENTS];

/* ---- event pump: runs on the sketch task ---- */

static void ble_pump()
{
    bl_ble_event_t ev;
    while (bl_ble_event_take(&ev)) {
        int h = ev.handle;
        switch (ev.type) {
        case BL_BLE_EV_CONNECT:
            s_server.conn_id_ = ev.conn_id;
            if (s_server.cb_) s_server.cb_->onConnect(&s_server);
            break;
        case BL_BLE_EV_DISCONNECT:
            s_server.conn_id_ = ev.conn_id;
            if (s_server.cb_) s_server.cb_->onDisconnect(&s_server);
            break;
        case BL_BLE_EV_WRITE:
            if (h >= 0 && h < MAX_CHARS && s_chars[h] && s_chars[h]->cb_) s_chars[h]->cb_->onWrite(s_chars[h]);
            break;
        case BL_BLE_EV_READ:
            if (h >= 0 && h < MAX_CHARS && s_chars[h] && s_chars[h]->cb_) s_chars[h]->cb_->onRead(s_chars[h]);
            break;
        case BL_BLE_EV_NOTIFY:
            if (h >= 0 && h < MAX_REMOTE && s_remote[h] && s_remote[h]->cb_) {
                s_remote[h]->cb_(s_remote[h], ev.data, ev.len, ev.flag != 0);
            }
            break;
        case BL_BLE_EV_CLIENT_CONNECT:
            if (h >= 0 && h < MAX_CLIENTS && s_clients[h] && s_clients[h]->cb_) s_clients[h]->cb_->onConnect(s_clients[h]);
            break;
        case BL_BLE_EV_CLIENT_DISCONNECT:
            if (h >= 0 && h < MAX_CLIENTS && s_clients[h] && s_clients[h]->cb_) s_clients[h]->cb_->onDisconnect(s_clients[h]);
            break;
        case BL_BLE_EV_SCAN_RESULT:
            if (s_scan.cb_) {
                bl_ble_adv_t a;
                if (bl_ble_scan_result(ev.index, &a) == BL_OK) {
                    s_scan.cb_->onResult(BLEAdvertisedDevice(a));
                }
            }
            break;
        case BL_BLE_EV_SCAN_DONE:
            if (s_scan.done_) {
                void (*done)(BLEScanResults) = s_scan.done_;
                s_scan.done_ = 0;
                done(s_scan.results_);
            }
            break;
        }
    }
}

/* ---- BLEUUID / BLEAddress ---- */

BLEUUID::BLEUUID(const char *uuid)
{
    if (uuid) {
        strncpy(text_, uuid, sizeof(text_) - 1);
    }
}

BLEUUID::BLEUUID(uint16_t uuid16)
{
    snprintf(text_, sizeof(text_), "%04x", uuid16);
}

BLEUUID::BLEUUID(uint32_t uuid32)
{
    snprintf(text_, sizeof(text_), "%08lx", (unsigned long)uuid32);
}

/* Compare as 128-bit UUIDs, so "180f" equals "0000180f-0000-1000-8000-00805f9b34fb". */
static void expand_uuid(const char *in, char out[37])
{
    size_t n = strlen(in);
    if (n == 4 || n == 8) {
        snprintf(out, 37, "%s%s-0000-1000-8000-00805f9b34fb", n == 4 ? "0000" : "", in);
    } else {
        strncpy(out, in, 36);
        out[36] = '\0';
    }
}

bool BLEUUID::equals(const BLEUUID &o) const
{
    char a[37], b[37];
    expand_uuid(text_, a);
    expand_uuid(o.text_, b);
    return strcasecmp(a, b) == 0;
}

BLEAddress::BLEAddress(const char *address, uint8_t type) : type_(type)
{
    if (address) {
        strncpy(text_, address, sizeof(text_) - 1);
    }
}

/* ---- BLEDevice ---- */

bool BLEDevice::init(const String &name)
{
    if (bl_ble_init(name.length() ? name.c_str() : 0) != BL_OK) {
        return false;
    }
    s_inited = true;
    bl_sketch_add_pump(ble_pump);
    return true;
}

void BLEDevice::deinit(bool)
{
    bl_ble_deinit();
    s_inited = false;
}

bool BLEDevice::getInitialized()
{
    return s_inited;
}

BLEServer *BLEDevice::createServer()
{
    if (!s_inited) {
        init();
    }
    bl_ble_server_create();
    return &s_server;
}

BLEAdvertising *BLEDevice::getAdvertising()
{
    return &s_advertising;
}

BLEScan *BLEDevice::getScan()
{
    if (!s_inited) {
        init();
    }
    return &s_scan;
}

BLEClient *BLEDevice::createClient()
{
    if (!s_inited) {
        init();
    }
    return new BLEClient();
}

BLEAddress BLEDevice::getAddress()
{
    char buf[18] = {};
    bl_ble_address(buf, sizeof(buf));
    return BLEAddress(buf);
}

esp_err_t BLEDevice::setMTU(uint16_t mtu)
{
    if (bl_ble_set_mtu(mtu) != BL_OK) {
        return ESP_FAIL;
    }
    s_mtu = mtu;
    return ESP_OK;
}

uint16_t BLEDevice::getMTU()
{
    return s_mtu;
}

/* ---- server side ---- */

BLEAdvertising *BLEServer::getAdvertising()
{
    return &s_advertising;
}

BLEService *BLEServer::createService(BLEUUID uuid, uint32_t numHandles, uint8_t)
{
    int h = bl_ble_service_create(uuid.c_str(), (int)numHandles);
    return h >= 0 ? new BLEService(h, uuid) : nullptr;
}

BLECharacteristic *BLEService::createCharacteristic(BLEUUID uuid, uint32_t properties)
{
    int h = bl_ble_char_create(h_, uuid.c_str(), properties);
    if (h < 0 || h >= MAX_CHARS) {
        return nullptr;
    }
    s_chars[h] = new BLECharacteristic(h, uuid);
    return s_chars[h];
}

String BLECharacteristic::getValue()
{
    uint8_t small[64];
    int n = bl_ble_char_get_value(h_, small, sizeof(small));
    if (n <= 0) {
        return String();
    }
    if (n <= (int)sizeof(small)) {
        return String((const char *)small, n);
    }
    char *big = (char *)malloc(n);
    if (!big) {
        return String();
    }
    bl_ble_char_get_value(h_, (uint8_t *)big, n);
    String s(big, n);
    free(big);
    return s;
}

void BLEAdvertising::setAdvertisementData(BLEAdvertisementData &d)
{
    if (d.manufacturer_.length()) {
        bl_ble_adv_config(BL_BLE_ADV_MANUFACTURER, 0, (const uint8_t *)d.manufacturer_.c_str(), d.manufacturer_.length());
    }
    if (d.name_.length()) {
        bl_ble_adv_config(BL_BLE_ADV_NAME, d.name_.c_str(), 0, 0);
    }
    if (d.appearance_) {
        bl_ble_adv_config(BL_BLE_ADV_APPEARANCE, 0, 0, d.appearance_);
    }
}

/* ---- scanning ---- */

BLEAdvertisedDevice BLEScanResults::getDevice(uint32_t i)
{
    bl_ble_adv_t a = {};
    bl_ble_scan_result((int)i, &a);
    return BLEAdvertisedDevice(a);
}

String BLEAdvertisedDevice::toString() const
{
    String s = "Name: " + getName() + ", Address: " + getAddress().toString();
    if (haveServiceUUID()) {
        s += ", serviceUUID: " + getServiceUUID().toString();
    }
    s += ", rssi: " + String(getRSSI());
    return s;
}

BLEScanResults *BLEScan::start(uint32_t seconds, bool is_continue)
{
    if (bl_ble_scan_start(seconds, active_, interval_, window_) != BL_OK) {
        return &results_;
    }
    while (bl_ble_scan_running()) {
        delay(10);      // onResult() callbacks run in here
    }
    delay(0);
    return &results_;
}

bool BLEScan::start(uint32_t seconds, void (*done)(BLEScanResults), bool is_continue)
{
    done_ = done;
    return bl_ble_scan_start(seconds, active_, interval_, window_) == BL_OK;
}

/* ---- client side ---- */

bool BLEClient::connect(BLEAddress address, uint8_t type, uint32_t)
{
    if (!s_inited) {
        BLEDevice::init();
    }
    int h = bl_ble_client_connect(address.c_str(), type);
    if (h < 0 || h >= MAX_CLIENTS) {
        return false;
    }
    h_ = h;
    s_clients[h] = this;    // onConnect() arrives through the event pump
    return true;
}

BLERemoteService *BLEClient::getService(BLEUUID uuid)
{
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (services_[i] && services_[i]->getUUID().equals(uuid)) {
            return services_[i];
        }
    }
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (!services_[i]) {
            services_[i] = new BLERemoteService(this, uuid);
            return services_[i];
        }
    }
    return nullptr;
}

BLERemoteCharacteristic *BLERemoteService::getCharacteristic(BLEUUID uuid)
{
    int h = bl_ble_remote_char(client_->h_, uuid_.c_str(), uuid.c_str());
    if (h < 0 || h >= MAX_REMOTE) {
        return nullptr;
    }
    if (!s_remote[h]) {
        s_remote[h] = new BLERemoteCharacteristic(h, uuid);
    }
    return s_remote[h];
}

String BLERemoteCharacteristic::readValue()
{
    uint8_t buf[244];
    int n = bl_ble_remote_read(h_, buf, sizeof(buf));
    return n > 0 ? String((const char *)buf, n < (int)sizeof(buf) ? n : sizeof(buf)) : String();
}
