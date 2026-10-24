/*
 * baseline_ble.h - the Arduino-ESP32 BLE library API for sketches:
 * BLEDevice, BLEServer, BLEService, BLECharacteristic, BLEDescriptor,
 * BLE2902/BLE2901, BLEAdvertising, BLEScan, BLEAdvertisedDevice, BLEClient,
 * BLERemoteService, BLERemoteCharacteristic, and the callback classes.
 *
 * The BLE stack runs inside Baseline (NimBLE). Callbacks (onConnect,
 * onWrite, onResult, notify callbacks, ...) run on the sketch task during
 * delay()/yield() or between loop() calls, never on the BLE task.
 * Differences from Arduino:
 *   - onRead() fires after the read was answered; set values ahead of time.
 *   - Descriptor values are fixed when addDescriptor() is called.
 *   - BLEClient::getService() cannot check that the service exists; the
 *     following getCharacteristic() returns nullptr if it does not.
 */
#ifndef BASELINE_BLE_H
#define BASELINE_BLE_H

#ifdef __cplusplus

#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1

/* Transmit power constants accepted by BLEDevice::setPower() (ignored). */
enum { ESP_PWR_LVL_N12 = 0, ESP_PWR_LVL_N9, ESP_PWR_LVL_N6, ESP_PWR_LVL_N3, ESP_PWR_LVL_N0,
       ESP_PWR_LVL_P3, ESP_PWR_LVL_P6, ESP_PWR_LVL_P9 };

class BLEServer;
class BLECharacteristic;
class BLEClient;
class BLERemoteCharacteristic;
class BLEAdvertisedDevice;

/* ---- UUID and address ---- */

class BLEUUID {
public:
    BLEUUID() {}
    BLEUUID(const char *uuid);
    BLEUUID(const String &uuid) : BLEUUID(uuid.c_str()) {}
    BLEUUID(uint16_t uuid16);
    BLEUUID(uint32_t uuid32);
    const char *c_str() const { return text_; }
    String toString() const { return String(text_); }
    bool equals(const BLEUUID &o) const;
    bool operator==(const BLEUUID &o) const { return equals(o); }
    bool operator!=(const BLEUUID &o) const { return !equals(o); }

private:
    char text_[37] = {};
};

class BLEAddress {
public:
    BLEAddress() {}
    BLEAddress(const char *address, uint8_t type = BLE_ADDR_PUBLIC);
    BLEAddress(const String &address, uint8_t type = BLE_ADDR_PUBLIC) : BLEAddress(address.c_str(), type) {}
    String toString() const { return String(text_); }
    const char *c_str() const { return text_; }
    uint8_t getType() const { return type_; }
    bool equals(const BLEAddress &o) const { return strcasecmp(text_, o.text_) == 0; }
    bool operator==(const BLEAddress &o) const { return equals(o); }

private:
    char text_[18] = {};
    uint8_t type_ = BLE_ADDR_PUBLIC;
};

/* ---- callback classes ---- */

class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() {}
    virtual void onConnect(BLEServer *server) {}
    virtual void onDisconnect(BLEServer *server) {}
};

class BLECharacteristicCallbacks {
public:
    virtual ~BLECharacteristicCallbacks() {}
    virtual void onWrite(BLECharacteristic *characteristic) {}
    virtual void onRead(BLECharacteristic *characteristic) {}
};

class BLEClientCallbacks {
public:
    virtual ~BLEClientCallbacks() {}
    virtual void onConnect(BLEClient *client) {}
    virtual void onDisconnect(BLEClient *client) {}
};

class BLEAdvertisedDeviceCallbacks {
public:
    virtual ~BLEAdvertisedDeviceCallbacks() {}
    virtual void onResult(BLEAdvertisedDevice device) = 0;
};

/* ---- GATT server side ---- */

class BLEDescriptor {
public:
    BLEDescriptor(const char *uuid, uint16_t max_len = 100) : uuid_(uuid) {}
    BLEDescriptor(BLEUUID uuid, uint16_t max_len = 100) : uuid_(uuid) {}
    virtual ~BLEDescriptor() {}
    void setValue(const uint8_t *data, size_t len) { value_ = String((const char *)data, len); }
    void setValue(const String &value) { value_ = value; }
    BLEUUID getUUID() const { return uuid_; }

protected:
    friend class BLECharacteristic;
    BLEUUID uuid_;
    String value_;
};

/* Client Characteristic Configuration: managed by the BLE stack. */
class BLE2902 : public BLEDescriptor {
public:
    BLE2902() : BLEDescriptor(BLEUUID((uint16_t)0x2902)) {}
    void setNotifications(bool) {}
    void setIndications(bool) {}
    bool getNotifications() { return false; }
    bool getIndications() { return false; }
};

/* Characteristic User Description. */
class BLE2901 : public BLEDescriptor {
public:
    BLE2901() : BLEDescriptor(BLEUUID((uint16_t)0x2901)) {}
    void setDescription(const String &text) { setValue(text); }
};

class BLECharacteristic {
public:
    static const uint32_t PROPERTY_READ = BL_BLE_PROP_READ;
    static const uint32_t PROPERTY_WRITE = BL_BLE_PROP_WRITE;
    static const uint32_t PROPERTY_NOTIFY = BL_BLE_PROP_NOTIFY;
    static const uint32_t PROPERTY_INDICATE = BL_BLE_PROP_INDICATE;
    static const uint32_t PROPERTY_WRITE_NR = BL_BLE_PROP_WRITE_NR;
    static const uint32_t PROPERTY_BROADCAST = BL_BLE_PROP_BROADCAST;

    BLECharacteristic(int handle, BLEUUID uuid) : h_(handle), uuid_(uuid) {}
    void setValue(const uint8_t *data, size_t len) { bl_ble_char_set_value(h_, data, len); }
    void setValue(const String &value) { setValue((const uint8_t *)value.c_str(), value.length()); }
    void setValue(uint16_t v) { setValue((const uint8_t *)&v, sizeof(v)); }
    void setValue(uint32_t v) { setValue((const uint8_t *)&v, sizeof(v)); }
    void setValue(int v) { setValue((const uint8_t *)&v, sizeof(v)); }
    void setValue(float v) { setValue((const uint8_t *)&v, sizeof(v)); }
    void setValue(double v) { setValue((const uint8_t *)&v, sizeof(v)); }
    String getValue();
    uint8_t *getData() { data_ = getValue(); return (uint8_t *)data_.c_str(); }
    size_t getLength() { int n = bl_ble_char_get_value(h_, 0, 0); return n > 0 ? (size_t)n : 0; }
    void notify(bool is_notification = true) { bl_ble_char_notify(h_, is_notification ? 0 : 1); }
    void indicate() { bl_ble_char_notify(h_, 1); }
    void setCallbacks(BLECharacteristicCallbacks *cb) { cb_ = cb; }
    void addDescriptor(BLEDescriptor *d)
    {
        if (d) bl_ble_char_add_descriptor(h_, d->uuid_.c_str(), (const uint8_t *)d->value_.c_str(), d->value_.length());
    }
    BLEUUID getUUID() const { return uuid_; }
    uint16_t getHandle() const { return (uint16_t)h_; }

    BLECharacteristicCallbacks *cb_ = 0;    // SDK internal

private:
    int h_;
    BLEUUID uuid_;
    String data_;
};

class BLEService {
public:
    BLEService(int handle, BLEUUID uuid) : h_(handle), uuid_(uuid) {}
    BLECharacteristic *createCharacteristic(const char *uuid, uint32_t properties) { return createCharacteristic(BLEUUID(uuid), properties); }
    BLECharacteristic *createCharacteristic(BLEUUID uuid, uint32_t properties);
    void start() { bl_ble_service_start(h_); }
    BLEUUID getUUID() const { return uuid_; }

private:
    int h_;
    BLEUUID uuid_;
};

class BLEAdvertisementData {
public:
    void setManufacturerData(const String &data) { manufacturer_ = data; }
    void setName(const String &name) { name_ = name; }
    void setFlags(uint8_t) {}
    void setAppearance(uint16_t appearance) { appearance_ = appearance; }
    String manufacturer_, name_;
    uint16_t appearance_ = 0;
};

class BLEAdvertising {
public:
    void addServiceUUID(const char *uuid) { bl_ble_adv_config(BL_BLE_ADV_SERVICE_UUID, uuid, 0, 0); }
    void addServiceUUID(BLEUUID uuid) { addServiceUUID(uuid.c_str()); }
    void setScanResponse(bool on) { bl_ble_adv_config(BL_BLE_ADV_SCAN_RESPONSE, 0, 0, on); }
    void setMinPreferred(uint16_t v) { bl_ble_adv_config(BL_BLE_ADV_MIN_PREFERRED, 0, 0, v); }
    void setMaxPreferred(uint16_t v) { bl_ble_adv_config(BL_BLE_ADV_MAX_PREFERRED, 0, 0, v); }
    void setAppearance(uint16_t v) { bl_ble_adv_config(BL_BLE_ADV_APPEARANCE, 0, 0, v); }
    void setMinInterval(uint16_t v) { bl_ble_adv_config(BL_BLE_ADV_MIN_INTERVAL, 0, 0, v); }
    void setMaxInterval(uint16_t v) { bl_ble_adv_config(BL_BLE_ADV_MAX_INTERVAL, 0, 0, v); }
    void setName(const String &name) { bl_ble_adv_config(BL_BLE_ADV_NAME, name.c_str(), 0, 0); }
    void setAdvertisementData(BLEAdvertisementData &d);
    void setScanResponseData(BLEAdvertisementData &d) { setAdvertisementData(d); }
    void reset() { bl_ble_adv_config(BL_BLE_ADV_RESET, 0, 0, 0); }
    bool start() { return bl_ble_adv_start() == BL_OK; }
    bool stop() { return bl_ble_adv_stop() == BL_OK; }
};

class BLEServer {
public:
    BLEService *createService(const char *uuid) { return createService(BLEUUID(uuid)); }
    BLEService *createService(BLEUUID uuid, uint32_t numHandles = 15, uint8_t inst_id = 0);
    void setCallbacks(BLEServerCallbacks *cb) { cb_ = cb; }
    uint32_t getConnectedCount() { return (uint32_t)bl_ble_server_connected_count(); }
    void startAdvertising() { bl_ble_adv_start(); }
    BLEAdvertising *getAdvertising();
    void disconnect(uint16_t conn_id) { bl_ble_server_disconnect(conn_id); }
    uint16_t getConnId() { return conn_id_; }

    BLEServerCallbacks *cb_ = 0;    // SDK internal
    uint16_t conn_id_ = 0;
};

/* ---- scanning ---- */

class BLEAdvertisedDevice {
public:
    BLEAdvertisedDevice() {}
    explicit BLEAdvertisedDevice(const bl_ble_adv_t &a) : a_(a) {}
    BLEAddress getAddress() const { return BLEAddress(a_.address, a_.address_type); }
    uint8_t getAddressType() const { return a_.address_type; }
    String getName() const { return String(a_.name); }
    int getRSSI() const { return a_.rssi; }
    int8_t getTXPower() const { return a_.tx_power; }
    uint16_t getAppearance() const { return a_.appearance; }
    BLEUUID getServiceUUID() const { return BLEUUID(a_.service_uuid); }
    String getManufacturerData() const { return String((const char *)a_.manufacturer_data, a_.manufacturer_len); }
    bool haveName() const { return a_.has_name; }
    bool haveRSSI() const { return true; }
    bool haveTXPower() const { return a_.has_tx_power; }
    bool haveAppearance() const { return a_.appearance != 0; }
    bool haveServiceUUID() const { return a_.has_service_uuid; }
    bool haveManufacturerData() const { return a_.has_manufacturer_data; }
    bool isAdvertisingService(BLEUUID uuid) const { return haveServiceUUID() && getServiceUUID().equals(uuid); }
    String toString() const;

private:
    bl_ble_adv_t a_ = {};
};

class BLEScanResults {
public:
    int getCount() { return bl_ble_scan_count(); }
    BLEAdvertisedDevice getDevice(uint32_t i);
    void dump() {}
};

class BLEScan {
public:
    void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks *cb, bool wantDuplicates = false, bool shouldParse = true) { cb_ = cb; }
    void setActiveScan(bool active) { active_ = active; }
    void setInterval(uint16_t ms) { interval_ = ms; }
    void setWindow(uint16_t ms) { window_ = ms; }
    /* Blocking: returns when the scan ends. onResult() callbacks run meanwhile. */
    BLEScanResults *start(uint32_t seconds, bool is_continue = false);
    /* Background: done() is called (on the sketch task) when the scan ends. */
    bool start(uint32_t seconds, void (*done)(BLEScanResults), bool is_continue = false);
    void stop() { bl_ble_scan_stop(); }
    void clearResults() { bl_ble_scan_clear(); }
    BLEScanResults *getResults() { return &results_; }

    BLEAdvertisedDeviceCallbacks *cb_ = 0;  // SDK internal
    void (*done_)(BLEScanResults) = 0;
    BLEScanResults results_;

private:
    bool active_ = false;
    uint16_t interval_ = 0;
    uint16_t window_ = 0;
};

/* ---- client side ---- */

class BLERemoteCharacteristic {
public:
    typedef void (*notify_callback)(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify);

    BLERemoteCharacteristic(int handle, BLEUUID uuid) : h_(handle), uuid_(uuid) {}
    String readValue();
    uint8_t readUInt8() { uint8_t v = 0; readRaw(&v, sizeof(v)); return v; }
    uint16_t readUInt16() { uint16_t v = 0; readRaw((uint8_t *)&v, sizeof(v)); return v; }
    uint32_t readUInt32() { uint32_t v = 0; readRaw((uint8_t *)&v, sizeof(v)); return v; }
    float readFloat() { float v = 0; readRaw((uint8_t *)&v, sizeof(v)); return v; }
    bool writeValue(uint8_t *data, size_t length, bool response = false) { return bl_ble_remote_write(h_, data, length, response) == BL_OK; }
    bool writeValue(const uint8_t *data, size_t length, bool response = false) { return bl_ble_remote_write(h_, data, length, response) == BL_OK; }
    bool writeValue(const String &value, bool response = false) { return writeValue((const uint8_t *)value.c_str(), value.length(), response); }
    bool writeValue(uint8_t value, bool response = false) { return writeValue(&value, 1, response); }
    bool canRead() { return bl_ble_remote_props(h_) & BL_BLE_PROP_READ; }
    bool canWrite() { return bl_ble_remote_props(h_) & BL_BLE_PROP_WRITE; }
    bool canWriteNoResponse() { return bl_ble_remote_props(h_) & BL_BLE_PROP_WRITE_NR; }
    bool canNotify() { return bl_ble_remote_props(h_) & BL_BLE_PROP_NOTIFY; }
    bool canIndicate() { return bl_ble_remote_props(h_) & BL_BLE_PROP_INDICATE; }
    bool canBroadcast() { return bl_ble_remote_props(h_) & BL_BLE_PROP_BROADCAST; }
    void registerForNotify(notify_callback cb, bool notifications = true, bool descriptorRequiresRegistration = true)
    {
        cb_ = cb;
        bl_ble_remote_subscribe(h_, cb ? (notifications ? 1 : 2) : 0);
    }
    BLEUUID getUUID() const { return uuid_; }

    notify_callback cb_ = 0;    // SDK internal

private:
    void readRaw(uint8_t *buf, size_t size) { bl_ble_remote_read(h_, buf, size); }
    int h_;
    BLEUUID uuid_;
};

class BLERemoteService {
public:
    BLERemoteService(BLEClient *client, BLEUUID uuid) : client_(client), uuid_(uuid) {}
    BLERemoteCharacteristic *getCharacteristic(const char *uuid) { return getCharacteristic(BLEUUID(uuid)); }
    BLERemoteCharacteristic *getCharacteristic(BLEUUID uuid);
    BLEUUID getUUID() const { return uuid_; }

private:
    BLEClient *client_;
    BLEUUID uuid_;
};

class BLEClient {
public:
    bool connect(BLEAddress address, uint8_t type = BLE_ADDR_PUBLIC, uint32_t timeoutMS = 0);
    bool connect(BLEAdvertisedDevice *device) { return device && connect(device->getAddress(), device->getAddressType()); }
    void disconnect() { if (h_ >= 0) bl_ble_client_disconnect(h_); }
    bool isConnected() { return h_ >= 0 && bl_ble_client_connected(h_); }
    int getRssi() { return h_ >= 0 ? bl_ble_client_rssi(h_) : 0; }
    BLERemoteService *getService(const char *uuid) { return getService(BLEUUID(uuid)); }
    BLERemoteService *getService(BLEUUID uuid);
    void setClientCallbacks(BLEClientCallbacks *cb) { cb_ = cb; }
    bool setMTU(uint16_t mtu) { return bl_ble_set_mtu(mtu) == BL_OK; }

    int h_ = -1;                    // SDK internal
    BLEClientCallbacks *cb_ = 0;

private:
    static const int MAX_SERVICES = 8;
    BLERemoteService *services_[MAX_SERVICES] = {};
};

/* ---- the device ---- */

class BLEDevice {
public:
    static bool init(const String &name = String());     // false if BLE could not start (or the chip has none)
    static void deinit(bool release_memory = false);
    static bool getInitialized();
    static BLEServer *createServer();
    static BLEAdvertising *getAdvertising();
    static void startAdvertising() { bl_ble_adv_start(); }
    static void stopAdvertising() { bl_ble_adv_stop(); }
    static BLEScan *getScan();
    static BLEClient *createClient();
    static BLEAddress getAddress();
    static esp_err_t setMTU(uint16_t mtu);
    static uint16_t getMTU();
    static void setPower(int level, int type = 0) {}
};

class BLEUtils {};

#endif /* __cplusplus */
#endif /* BASELINE_BLE_H */
