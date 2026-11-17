/*
 * bl_rp2_ble.cpp - Bluetooth LE for RP2350 boards with a CYW43 radio (the
 * Thing Plus RP2350's RM2), on arduino-pico's BLE library and the BTstack it
 * runs on. The same table entries as the ESP32 Baselines
 * (baseline/src/bl_impl_ble.cpp): the sketch's BLEDevice, BLEServer, BLEScan,
 * BLEClient, ... and BLE serial (the Nordic UART Service) wrap these by handle.
 *
 * Built only for boards whose bl_board.h defines BL_HAS_BLE; elsewhere the
 * entries stay the weak "unsupported" ones.
 *
 * What comes from where:
 *  - the library: starting the stack (BLE.begin), the GATT server (BLEServer,
 *    BLEService, BLECharacteristic: it builds BTstack's attribute database
 *    when advertising starts, and answers reads and writes);
 *  - BTstack directly, where the library has no call for it: advertising and
 *    scan response data, background scans, notifications and indications,
 *    and the GATT client (the library's client discovers the whole database
 *    on connect and mixes up server and client connections).
 *
 * Context: BTstack runs from arduino-pico's async context, a low-priority
 * interrupt on core 0 ("threadsafe background"). Its callbacks interrupt the
 * sketch at any point, so they only queue bl_ble_event_t records and copy data
 * into Baseline's tables; the sketch takes them with ble_event_take() (the
 * SDK's pump). Shared tables are guarded with interrupts off (IrqOff, short
 * copies only). Calls into BTstack from the sketch's side hold the async
 * context lock (BluetoothLock), which keeps the callbacks out meanwhile. A
 * blocking entry waits with the lock released, so the callbacks it waits for
 * can run.
 *
 * Limits of this port: one GATT client connection (BTstack is configured for
 * one), one central connected to the server at a time, a 512-byte attribute
 * database (MAX_ATT_DB_SIZE: about eight 128-bit characteristics), and the
 * stack stays powered after ble_deinit() (a restart turns it off).
 */
#include "bl_rp2.h"

#if defined(BL_HAS_BLE)

#include <new>
#include <BLE.h>
#include <btstack.h>
#include <BluetoothLock.h>
#include <hardware/sync.h>

#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // central writes here
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // we notify here
#define NUS_BUFFER  1024

#define EVENT_QUEUE_LEN 16
#define MAX_SERVICES    12
#define MAX_CHARS       32
#define MAX_RESULTS     40
#define MAX_REMOTE      16
#define MAX_ADV_UUIDS   4
#define MAX_PERIPH      2           // MAX_NR_HCI_CONNECTIONS in arduino-pico's btstack_config.h
#define OP_TIMEOUT_MS   5000
#define CONNECT_MS      5000
#define NO_CON          HCI_CON_HANDLE_INVALID

#ifdef MAX_ATT_DB_SIZE
#define DB_LIMIT        MAX_ATT_DB_SIZE
#else
#define DB_LIMIT        512
#endif
/* Bytes of BTstack's attribute database (att_db_util.c): version byte, end
 * tag, and the GAP service with the device name the library always adds. */
#define DB_BASE         (1 + 2 + 31)

/* ---- helpers ---- */

struct IrqOff {
    uint32_t s;
    IrqOff() : s(save_and_disable_interrupts()) {}
    ~IrqOff() { restore_interrupts(s); }
};

/* Waits until done() or ms have passed; BTstack's callbacks run meanwhile. */
template <typename F> static bool wait_until(F done, uint32_t ms)
{
    uint32_t start = millis();
    while (!done()) {
        if (millis() - start >= ms) {
            return false;
        }
        delay(1);
    }
    return true;
}

/* A UUID as text: "180d", "0000180d", "0x180D" or the 36-character form.
 * Base UUIDs (xxxxxxxx-0000-1000-8000-00805f9b34fb with a 16-bit value) are
 * kept as 16-bit ones, as peers expect. u128 is in text order (big-endian),
 * BTstack's and the library's convention. */
struct Uuid {
    bool is16;
    uint16_t u16;
    uint8_t u128[16];
};

static const uint8_t BASE_UUID[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                       0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB };

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_uuid(const char *s, Uuid &u)
{
    if (!s) {
        return false;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    uint8_t b[16] = {};
    int n = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '-') {
            continue;
        }
        int v = hex_value(*p);
        if (v < 0 || n >= 32) {
            return false;
        }
        b[n / 2] |= (uint8_t)(n % 2 ? v : v << 4);
        n++;
    }
    memset(&u, 0, sizeof(u));
    if (n == 4) {
        u.is16 = true;
        u.u16 = (uint16_t)(b[0] << 8 | b[1]);
        return true;
    }
    if (n == 8) {
        memcpy(u.u128, BASE_UUID, 16);
        memcpy(u.u128, b, 4);
    } else if (n == 32) {
        memcpy(u.u128, b, 16);
    } else {
        return false;
    }
    if (u.u128[0] == 0 && u.u128[1] == 0 && memcmp(u.u128 + 4, BASE_UUID + 4, 12) == 0) {
        u.is16 = true;
        u.u16 = (uint16_t)(u.u128[2] << 8 | u.u128[3]);
    }
    return true;
}

static BLEUUID lib_uuid(const Uuid &u)
{
    return u.is16 ? BLEUUID(u.u16) : BLEUUID(u.u128);
}

static bool same_uuid(const Uuid &a, const Uuid &b)
{
    return a.is16 == b.is16 && (a.is16 ? a.u16 == b.u16 : memcmp(a.u128, b.u128, 16) == 0);
}

/* The 36-character form Arduino-ESP32's BLEUUID::toString() gives. */
static void uuid128_text(const uint8_t *u, char out[37])
{
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

static void uuid32_text(uint32_t v, char out[37])
{
    snprintf(out, 37, "%08lx-0000-1000-8000-00805f9b34fb", (unsigned long)v);
}

static void addr_text(const bd_addr_t a, char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* A byte FIFO filled on one side of the interrupt boundary and emptied on the other. */
struct Ring {
    uint8_t *buf = nullptr;
    uint16_t size = 0, head = 0, count = 0;

    bool alloc(uint16_t n)
    {
        if (!buf) {
            buf = (uint8_t *)malloc(n);
            size = buf ? n : 0;
        }
        return buf != nullptr;
    }
    size_t put(const uint8_t *d, size_t n)
    {
        IrqOff off;
        size_t k = 0;
        while (k < n && count < size) {
            buf[(head + count) % size] = d[k++];
            count++;
        }
        return k;
    }
    size_t get(uint8_t *d, size_t n)
    {
        IrqOff off;
        size_t k = 0;
        while (k < n && count) {
            d[k++] = buf[head];
            head = (uint16_t)((head + 1) % size);
            count--;
        }
        return k;
    }
    size_t avail()
    {
        IrqOff off;
        return count;
    }
    void clear()
    {
        IrqOff off;
        head = count = 0;
    }
};

/* ---- events ---- */

static bl_ble_event_t *s_ev;
static uint8_t s_ev_head, s_ev_count;

static void push_event(const bl_ble_event_t &ev)
{
    if (!s_ev) {
        return;
    }
    IrqOff off;
    if (s_ev_count == EVENT_QUEUE_LEN) {        // full: keep the newest events
        s_ev_head = (uint8_t)((s_ev_head + 1) % EVENT_QUEUE_LEN);
        s_ev_count--;
    }
    s_ev[(s_ev_head + s_ev_count) % EVENT_QUEUE_LEN] = ev;
    s_ev_count++;
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

/* ---- server characteristics ----
 * The library's characteristic, with access to what it keeps protected: the
 * attribute handle, the CCCD the central wrote, and the value without the
 * library's setValue() (which sends a notification on every change). */
class BlChar : public BLECharacteristic {
public:
    BlChar(BLEUUID u, uint16_t props) : BLECharacteristic(u, props) {}
    ~BlChar() override
    {
        free(_charData);
        _charData = nullptr;
        free(snap);
    }

    uint16_t value_handle() const { return _valueHandle; }
    uint16_t cccd() const { return _configData; }
    void forget_cccd()
    {
        _configData = 0;
        _notificationEnabled = false;
    }
    bool set_raw(const uint8_t *d, size_t n)
    {
        void *p = realloc(_charData, n + 1);
        if (!p) {
            return false;
        }
        _charData = p;
        if (n) {
            memcpy(p, d, n);
        }
        ((uint8_t *)p)[n] = 0;
        _charLen = n;
        return true;
    }
    size_t len() const { return _charLen; }
    const uint8_t *data() const { return (const uint8_t *)_charData; }
    bool has_description() const { return _description != nullptr; }
    bool set_description(const uint8_t *v, size_t n)
    {
        char *p = (char *)malloc(n + 1);
        if (!p) {
            return false;
        }
        if (n) {
            memcpy(p, v, n);
        }
        p[n] = 0;
        free(_description);
        _description = p;
        _descriptionLen = n;
        return true;
    }
    /* Copies the value to send now (up to max bytes). */
    bool snapshot(size_t max)
    {
        size_t n = _charLen < max ? _charLen : max;
        if (n > snap_cap) {
            uint8_t *p = (uint8_t *)realloc(snap, n);
            if (!p) {
                return false;
            }
            snap = p;
            snap_cap = (uint16_t)n;
        }
        if (n) {
            memcpy(snap, _charData, n);
        }
        snap_len = (uint16_t)n;
        return true;
    }

    /* A notification or indication waiting for BTstack's "can send now". */
    btstack_context_callback_registration_t send_reg = {};
    volatile bool send_pending = false;
    bool send_indicate = false;
    uint16_t send_con = NO_CON;
    uint8_t *snap = nullptr;
    uint16_t snap_len = 0, snap_cap = 0;
};

/* The services the library's server holds (protected), to empty it in ble_deinit(). */
struct ServerPeek : BLEServer {
    static std::list<BLEService *> &services(BLEServer *s) { return s->*(&ServerPeek::_svc); }
};

/* ---- shared state ---- */

static bool s_stack_up;             // BLE.begin() has run; the controller stays on until a restart
static bool s_inited;
static char s_name[30];
static btstack_packet_callback_registration_t s_hci_reg;

static BLEServer *s_server;
static BLEService *s_services[MAX_SERVICES];
static bool s_svc_started[MAX_SERVICES];
static BlChar *s_chars[MAX_CHARS];
static int s_db_bytes = DB_BASE;    // the attribute database once every created service is in it
static bool s_db_dirty = true;      // services or characteristics changed since it was built
static bool s_att_up;               // the library has built the database and started the ATT server
static bool s_advertising;

static volatile uint16_t s_periph[MAX_PERIPH] = { NO_CON, NO_CON };
static volatile int s_periph_count;

/* advertising settings (ble_adv_config) */
static Uuid s_adv_uuids[MAX_ADV_UUIDS];
static int s_adv_uuid_count;
static bool s_scan_resp = true;     // Arduino-ESP32's default: the name goes in the scan response
static char s_adv_name[30];
static uint8_t s_manuf[29];
static uint8_t s_manuf_len;
static uint16_t s_appearance;
static uint16_t s_pref_min, s_pref_max;
static uint16_t s_adv_min = 0x30, s_adv_max = 0x30;   // 0.625 ms units: 30 ms, the library's default
static uint8_t s_adv_buf[31], s_adv_len;
static uint8_t s_sr_buf[31], s_sr_len;

/* BLE serial */
static BLEService *s_nus_svc;
static BlChar *s_nus_tx, *s_nus_rx_char;
static Ring s_nus_in, s_nus_out;
static btstack_context_callback_registration_t s_nus_reg;
static volatile bool s_nus_pending;

static uint16_t first_periph(void)
{
    for (int i = 0; i < MAX_PERIPH; i++) {
        if (s_periph[i] != NO_CON) {
            return s_periph[i];
        }
    }
    return NO_CON;
}

/* ---- client state ---- */

struct Remote {
    bool used;
    bool listening;
    Uuid svc, chr;
    uint16_t con;                       // the connection it was found on; NO_CON once that is gone
    gatt_client_characteristic_t ch;
    gatt_client_notification_t notif;
};
static Remote *s_remote;                // MAX_REMOTE entries, allocated on first use

static volatile uint16_t s_client_con = NO_CON;
static volatile bool s_client_connecting, s_client_failed;
static volatile bool s_rssi_done;
static volatile int8_t s_rssi;

enum { OP_NONE, OP_SERVICE, OP_CHAR, OP_READ, OP_WRITE, OP_CCCD };
static volatile uint8_t s_op = OP_NONE;
static volatile bool s_op_done = true;
static volatile uint8_t s_op_status;
#define OP_LOST 0xFF                    // s_op_status: the connection went away
static gatt_client_service_t s_found_service;
static volatile bool s_found_svc;
static gatt_client_characteristic_t s_found_char;
static volatile bool s_found_chr;
static uint8_t *s_read_buf;
static size_t s_read_size;
static volatile size_t s_read_len;
static uint8_t *s_write_buf;

/* ---- scanning state ---- */

static bl_ble_adv_t *s_results;         // MAX_RESULTS entries, allocated on the first scan
static int s_result_count;              // guarded by IrqOff
static volatile bool s_scanning;
static btstack_timer_source_t s_scan_timer;

/* ---- BTstack callbacks (async context) ---- */

static void client_lost(void)
{
    s_client_con = NO_CON;
    if (s_remote) {
        for (int i = 0; i < MAX_REMOTE; i++) {
            if (s_remote[i].listening) {
                gatt_client_stop_listening_for_characteristic_value_updates(&s_remote[i].notif);
                s_remote[i].listening = false;
            }
            s_remote[i].con = NO_CON;
        }
    }
    if (!s_op_done) {
        s_op = OP_NONE;
        s_op_status = OP_LOST;
        s_read_buf = nullptr;
        s_op_done = true;
    }
    s_rssi_done = true;
    push_simple(BL_BLE_EV_CLIENT_DISCONNECT, 0);
}

static bool periph_remove(uint16_t h)
{
    for (int i = 0; i < MAX_PERIPH; i++) {
        if (s_periph[i] == h) {
            s_periph[i] = NO_CON;
            s_periph_count--;
            // BTstack forgets the connection's pending sends; a new central starts unsubscribed
            for (int c = 0; c < MAX_CHARS; c++) {
                if (s_chars[c]) {
                    s_chars[c]->send_pending = false;
                    s_chars[c]->forget_cccd();
                }
            }
            if (s_nus_tx) {
                s_nus_tx->forget_cccd();
                s_nus_pending = false;
                s_nus_out.clear();
            }
            return true;
        }
    }
    return false;
}

static void scan_report(const uint8_t *packet)
{
    BLE.clearScan();    // the library keeps a list of every device it hears; Baseline has its own table
    if (!s_scanning || !s_results) {
        return;
    }
    bl_ble_adv_t r = {};
    bd_addr_t a;
    gap_event_advertising_report_get_address(packet, a);
    addr_text(a, r.address);
    r.address_type = gap_event_advertising_report_get_address_type(packet);
    r.rssi = (int8_t)gap_event_advertising_report_get_rssi(packet);
    ad_context_t ctx;
    for (ad_iterator_init(&ctx, gap_event_advertising_report_get_data_length(packet),
                          gap_event_advertising_report_get_data(packet));
         ad_iterator_has_more(&ctx); ad_iterator_next(&ctx)) {
        uint8_t type = ad_iterator_get_data_type(&ctx);
        uint8_t n = ad_iterator_get_data_len(&ctx);
        const uint8_t *d = ad_iterator_get_data(&ctx);
        switch (type) {
        case BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME:
        case BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME: {
            size_t k = n < sizeof(r.name) - 1 ? n : sizeof(r.name) - 1;
            memcpy(r.name, d, k);
            r.name[k] = 0;
            r.has_name = 1;
            break;
        }
        case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
        case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
            if (!r.has_service_uuid && n >= 2) {
                uuid32_text(little_endian_read_16(d, 0), r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
        case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
            if (!r.has_service_uuid && n >= 4) {
                uuid32_text(little_endian_read_32(d, 0), r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
        case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
            if (!r.has_service_uuid && n >= 16) {
                uint8_t be[16];
                reverse_128(d, be);
                uuid128_text(be, r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case BLUETOOTH_DATA_TYPE_TX_POWER_LEVEL:
            if (n >= 1) {
                r.tx_power = (int8_t)d[0];
                r.has_tx_power = 1;
            }
            break;
        case BLUETOOTH_DATA_TYPE_APPEARANCE:
            if (n >= 2) {
                r.appearance = little_endian_read_16(d, 0);
            }
            break;
        case BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA:
            r.manufacturer_len = (uint8_t)(n < sizeof(r.manufacturer_data) ? n : sizeof(r.manufacturer_data));
            memcpy(r.manufacturer_data, d, r.manufacturer_len);
            r.has_manufacturer_data = 1;
            break;
        default:
            break;
        }
    }
    // An advertisement and its scan response arrive as two reports: merge them.
    int index = -1;
    bool is_new = false;
    {
        IrqOff off;
        for (int i = 0; i < s_result_count; i++) {
            if (strcmp(s_results[i].address, r.address) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0 && s_result_count < MAX_RESULTS) {
            index = s_result_count++;
            is_new = true;
            s_results[index] = r;
        } else if (index >= 0) {
            bl_ble_adv_t &o = s_results[index];
            o.rssi = r.rssi;
            if (r.has_name) {
                memcpy(o.name, r.name, sizeof(o.name));
                o.has_name = 1;
            }
            if (r.has_service_uuid) {
                memcpy(o.service_uuid, r.service_uuid, sizeof(o.service_uuid));
                o.has_service_uuid = 1;
            }
            if (r.has_tx_power) {
                o.tx_power = r.tx_power;
                o.has_tx_power = 1;
            }
            if (r.appearance) {
                o.appearance = r.appearance;
            }
            if (r.has_manufacturer_data) {
                memcpy(o.manufacturer_data, r.manufacturer_data, sizeof(o.manufacturer_data));
                o.manufacturer_len = r.manufacturer_len;
                o.has_manufacturer_data = 1;
            }
        }
    }
    if (is_new) {
        push_simple(BL_BLE_EV_SCAN_RESULT, -1, 0, index);  // once per device, as in Arduino
    }
}

static void hci_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
            hci_con_handle_t h = gap_subevent_le_connection_complete_get_connection_handle(packet);
            if (status == ERROR_CODE_SUCCESS && gap_subevent_le_connection_complete_get_role(packet) == HCI_ROLE_SLAVE) {
                for (int i = 0; i < MAX_PERIPH; i++) {
                    if (s_periph[i] == NO_CON) {
                        s_periph[i] = h;
                        s_periph_count++;
                        break;
                    }
                }
                push_simple(BL_BLE_EV_CONNECT, 0, h);
            } else if (s_client_connecting) {
                s_client_connecting = false;
                if (status == ERROR_CODE_SUCCESS) {
                    s_client_con = h;
                    push_simple(BL_BLE_EV_CLIENT_CONNECT, 0);
                } else {
                    s_client_failed = true;
                }
            }
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        uint16_t h = hci_event_disconnection_complete_get_connection_handle(packet);
        if (periph_remove(h)) {
            // BTstack advertises again by itself while advertising is on
            push_simple(BL_BLE_EV_DISCONNECT, 0, h);
        } else if (h == s_client_con) {
            client_lost();
        }
        break;
    }
    case GAP_EVENT_ADVERTISING_REPORT:
        scan_report(packet);
        break;
    case GAP_EVENT_RSSI_MEASUREMENT:
        if (gap_event_rssi_measurement_get_con_handle(packet) == s_client_con) {
            s_rssi = (int8_t)gap_event_rssi_measurement_get_rssi(packet);
            s_rssi_done = true;
        }
        break;
    default:
        break;
    }
}

static void remote_value(uint16_t con, uint16_t value_handle, const uint8_t *data, uint16_t len, uint8_t is_notify)
{
    if (!s_remote) {
        return;
    }
    for (int i = 0; i < MAX_REMOTE; i++) {
        Remote &r = s_remote[i];
        if (r.used && r.listening && r.con == con && r.ch.value_handle == value_handle) {
            push_data(BL_BLE_EV_NOTIFY, i, data, len, is_notify);
            return;
        }
    }
}

static void read_part(uint16_t offset, const uint8_t *data, uint16_t len)
{
    if (s_read_buf && offset < s_read_size) {
        size_t n = s_read_size - offset < len ? s_read_size - offset : len;
        memcpy(s_read_buf + offset, data, n);
    }
    if ((size_t)offset + len > s_read_len) {
        s_read_len = (size_t)offset + len;
    }
}

static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT:
        if (s_op == OP_SERVICE && !s_found_svc) {
            gatt_event_service_query_result_get_service(packet, &s_found_service);
            s_found_svc = true;
        }
        break;
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
        if (s_op == OP_CHAR && !s_found_chr) {
            gatt_event_characteristic_query_result_get_characteristic(packet, &s_found_char);
            s_found_chr = true;
        }
        break;
    case GATT_EVENT_CHARACTERISTIC_VALUE_QUERY_RESULT:
        if (s_op == OP_READ) {
            read_part(0, gatt_event_characteristic_value_query_result_get_value(packet),
                      gatt_event_characteristic_value_query_result_get_value_length(packet));
        }
        break;
    case GATT_EVENT_QUERY_COMPLETE:
        if (s_op != OP_NONE) {
            s_op_status = gatt_event_query_complete_get_att_status(packet);
            s_op = OP_NONE;
            s_op_done = true;
        }
        break;
    case GATT_EVENT_NOTIFICATION:
        remote_value(gatt_event_notification_get_handle(packet), gatt_event_notification_get_value_handle(packet),
                     gatt_event_notification_get_value(packet), gatt_event_notification_get_value_length(packet), 1);
        break;
    case GATT_EVENT_INDICATION:
        remote_value(gatt_event_indication_get_handle(packet), gatt_event_indication_get_value_handle(packet),
                     gatt_event_indication_get_value(packet), gatt_event_indication_get_value_length(packet), 0);
        break;
    default:
        break;
    }
}

/* ---- start-up ---- */

static void adv_defaults(void)
{
    s_adv_uuid_count = 0;
    s_scan_resp = true;
    strlcpy(s_adv_name, s_name, sizeof(s_adv_name));
    s_manuf_len = 0;
    s_appearance = 0;
    s_pref_min = s_pref_max = 0;
    s_adv_min = s_adv_max = 0x30;
}

/* Start BLE once. Returns false if the event queue could not be allocated.
 * The name only counts on the first call (and the first after ble_deinit),
 * as with Arduino-ESP32's BLEDevice::init(). */
static bool ensure_init(const char *name)
{
    if (s_inited) {
        return true;
    }
    strlcpy(s_name, name && *name ? name : "Baseline", sizeof(s_name));
    if (!s_ev) {
        s_ev = (bl_ble_event_t *)calloc(EVENT_QUEUE_LEN, sizeof(bl_ble_event_t));
        if (!s_ev) {
            return false;
        }
    }
    if (!s_stack_up) {
        {
            BlWdogLong wdt;     // the radio's Bluetooth firmware loads on the first start
            BLE.begin(String(s_name));
        }
        BluetoothLock lock;
        s_hci_reg.callback = hci_handler;
        hci_add_event_handler(&s_hci_reg);
        gatt_client_init();
        s_stack_up = true;
    } else if (s_server) {
        BluetoothLock lock;
        s_server->setName(s_name);
    }
    adv_defaults();
    s_inited = true;
    return true;
}

static BLEServer *ensure_server()
{
    if (!ensure_init(NULL)) {
        return NULL;
    }
    if (!s_server) {
        BluetoothLock lock;
        s_server = BLE.server();
        s_server->setName(s_name);
    }
    return s_server;
}

/* ---- advertising ---- */

/* Builds the advertisement and scan response from the settings and hands
 * them to BTstack. Holds the BluetoothLock. */
static void apply_adv(void)
{
    uint8_t len = 0;
    auto put = [&](uint8_t *buf, uint8_t &n, uint8_t type, const uint8_t *d, uint8_t k) -> bool {
        if (n + 2 + k > 31) {
            return false;
        }
        buf[n++] = (uint8_t)(k + 1);
        buf[n++] = type;
        memcpy(buf + n, d, k);
        n = (uint8_t)(n + k);
        return true;
    };
    const uint8_t flags = 0x06;     // LE general discoverable, no BR/EDR
    put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_FLAGS, &flags, 1);
    uint8_t list[16 * MAX_ADV_UUIDS];
    uint8_t k = 0;
    for (int i = 0; i < s_adv_uuid_count; i++) {
        if (s_adv_uuids[i].is16) {
            little_endian_store_16(list, k, s_adv_uuids[i].u16);
            k = (uint8_t)(k + 2);
        }
    }
    if (k) {
        put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, list, k);
    }
    k = 0;
    for (int i = 0; i < s_adv_uuid_count; i++) {
        if (!s_adv_uuids[i].is16) {
            reverse_128(s_adv_uuids[i].u128, list + k);
            k = (uint8_t)(k + 16);
        }
    }
    if (k) {
        // more than one 128-bit UUID does not fit: advertise the first
        put(s_adv_buf, len, k > 16 ? BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS
                                   : BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS, list, 16);
    }
    if (s_appearance) {
        uint8_t a[2];
        little_endian_store_16(a, 0, s_appearance);
        put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_APPEARANCE, a, 2);
    }
    if (s_manuf_len) {
        put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA, s_manuf, s_manuf_len);
    }
    if (s_pref_min || s_pref_max) {
        uint8_t r[4];
        little_endian_store_16(r, 0, s_pref_min ? s_pref_min : 0xFFFF);     // 0xFFFF: no preference
        little_endian_store_16(r, 2, s_pref_max ? s_pref_max : 0xFFFF);
        put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_SLAVE_CONNECTION_INTERVAL_RANGE, r, 4);
    }
    uint8_t name_len = (uint8_t)strlen(s_adv_name);
    // The complete name goes in the advertisement when it fits, and always in
    // the scan response when that is on.
    if (name_len && !put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, (const uint8_t *)s_adv_name, name_len) &&
        !s_scan_resp && len + 3 <= 31) {
        put(s_adv_buf, len, BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME, (const uint8_t *)s_adv_name, (uint8_t)(31 - len - 2));
    }
    s_adv_len = len;
    s_sr_len = 0;
    if (s_scan_resp && name_len) {
        put(s_sr_buf, s_sr_len, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, (const uint8_t *)s_adv_name,
            name_len <= 29 ? name_len : 29);
    }
    uint16_t lo = s_adv_min, hi = s_adv_max;
    if (lo < 0x20) lo = 0x20;           // 20 ms, the shortest the specification allows
    if (hi < lo) hi = lo;
    bd_addr_t none = {};
    gap_advertisements_set_params(lo, hi, 0 /* ADV_IND: connectable */, 0, none, 0x07, 0);
    gap_advertisements_set_data(s_adv_len, s_adv_buf);
    gap_scan_response_set_data(s_sr_len, s_sr_buf);
}

/* Starts advertising. The library builds BTstack's attribute database from
 * the server's services when advertising starts; it is rebuilt when services
 * changed, unless a central is connected (its handles would move under it):
 * then the change waits for the next start. Holds the BluetoothLock. */
static void advertise(void)
{
    if (!s_att_up || (s_db_dirty && s_periph_count == 0)) {
        BLE.startAdvertising(false);
        s_att_up = true;
        s_db_dirty = false;
    }
    apply_adv();
    gap_advertisements_enable(1);
    s_advertising = true;
}

/* ---- BLE serial (Nordic UART Service) ---- */

/* Sends what the buffer holds, a chunk of up to MTU - 3 bytes per notification,
 * while the controller has room; then asks BTstack to call back when it has
 * room again. Called with BTstack's lock held or from its own context.
 * BTstack answers a send request only while it processes radio events, so a
 * request made from the sketch with nothing else going on would wait forever:
 * the sketch side sends directly whenever it can. */
static void nus_can_send(void *);

static void nus_drain(void)
{
    static uint8_t chunk[244];
    uint16_t con = first_periph();
    if (!s_nus_tx || con == NO_CON || !(s_nus_tx->cccd() & 1)) {
        s_nus_out.clear();      // not subscribed: dropped, as on the ESP32
        return;
    }
    uint16_t mtu = att_server_get_mtu(con);
    size_t max = mtu > 3 ? mtu - 3 : 20;
    while (s_nus_out.avail() && att_server_can_send_packet_now(con)) {
        size_t n = s_nus_out.get(chunk, max < sizeof(chunk) ? max : sizeof(chunk));
        if (!n || att_server_notify(con, s_nus_tx->value_handle(), chunk, (uint16_t)n) != ERROR_CODE_SUCCESS) {
            break;
        }
    }
    if (s_nus_out.avail() && !s_nus_pending) {
        s_nus_reg.callback = nus_can_send;
        s_nus_reg.context = nullptr;
        if (att_server_request_to_send_notification(&s_nus_reg, con) == ERROR_CODE_SUCCESS) {
            s_nus_pending = true;
        }
    }
}

static void nus_can_send(void *)
{
    s_nus_pending = false;
    nus_drain();
}

static void nus_kick(void)
{
    BluetoothLock lock;
    nus_drain();
}

class NusRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        s_nus_in.put((const uint8_t *)c->valueData(), c->valueLen());
    }
};
static NusRxCallbacks s_nus_rx_cb;

/* Attribute database bytes for a service or a characteristic (att_db_util.c). */
static int db_service_bytes(const Uuid &u)
{
    return u.is16 ? 10 : 24;
}

static int db_char_bytes(const Uuid &u, uint16_t props)
{
    return (u.is16 ? 13 + 8 : 27 + 22) + ((props & (ATT_PROPERTY_NOTIFY | ATT_PROPERTY_INDICATE)) ? 10 : 0);
}

static void adv_add_uuid(const Uuid &u)
{
    for (int i = 0; i < s_adv_uuid_count; i++) {
        if (same_uuid(s_adv_uuids[i], u)) {
            return;
        }
    }
    if (s_adv_uuid_count < MAX_ADV_UUIDS) {
        s_adv_uuids[s_adv_uuid_count++] = u;
    }
}

extern "C" int bl_impl_ble_serial_begin(const char *name)
{
    if (s_nus_tx) {
        return BL_ERR_STATE;
    }
    Uuid su, tu, ru;
    parse_uuid(NUS_SERVICE, su);
    parse_uuid(NUS_TX, tu);
    parse_uuid(NUS_RX, ru);
    const uint16_t tx_props = ATT_PROPERTY_NOTIFY;
    const uint16_t rx_props = ATT_PROPERTY_WRITE | ATT_PROPERTY_WRITE_WITHOUT_RESPONSE;
    int need = db_service_bytes(su) + db_char_bytes(tu, tx_props) + db_char_bytes(ru, rx_props);
    if (!s_nus_in.alloc(NUS_BUFFER) || !s_nus_out.alloc(NUS_BUFFER) || !ensure_init(name) || !ensure_server() ||
        s_db_bytes + need > DB_LIMIT) {
        return BL_ERR_NO_MEM;
    }
    BLEService *svc = new (std::nothrow) BLEService(lib_uuid(su));
    BlChar *tx = new (std::nothrow) BlChar(lib_uuid(tu), tx_props);
    BlChar *rx = new (std::nothrow) BlChar(lib_uuid(ru), rx_props);
    if (!svc || !tx || !rx) {
        delete svc;
        delete tx;
        delete rx;
        return BL_ERR_NO_MEM;
    }
    rx->setCallbacks(&s_nus_rx_cb);
    s_nus_in.clear();
    s_nus_out.clear();
    s_db_bytes += need;
    adv_add_uuid(su);
    s_scan_resp = true;
    BluetoothLock lock;
    svc->addCharacteristic(tx);
    svc->addCharacteristic(rx);
    s_server->addService(svc);
    s_nus_svc = svc;
    s_nus_tx = tx;
    s_nus_rx_char = rx;
    s_db_dirty = true;
    advertise();
    return BL_OK;
}

extern "C" int bl_impl_ble_serial_connected(void)
{
    return s_periph_count > 0 ? 1 : 0;
}

extern "C" int bl_impl_ble_serial_write(const uint8_t *data, size_t len)
{
    if (!s_nus_tx || s_periph_count <= 0 || !data) {
        return 0;
    }
    size_t done = 0;
    uint32_t start = millis();
    while (done < len) {
        done += s_nus_out.put(data + done, len - done);
        nus_kick();
        if (done < len) {
            // the buffer is full: wait for BTstack to send some (up to 1 s)
            if (millis() - start > 1000 || s_periph_count <= 0) {
                break;
            }
            delay(1);
        }
    }
    return (int)done;
}

extern "C" int bl_impl_ble_serial_available(void)
{
    return s_nus_in.buf ? (int)s_nus_in.avail() : 0;
}

extern "C" int bl_impl_ble_serial_read(void)
{
    uint8_t c;
    return (s_nus_in.buf && s_nus_in.get(&c, 1) == 1) ? c : -1;
}

/* ---- device ---- */

extern "C" int bl_impl_ble_init(const char *name)
{
    return ensure_init(name) ? BL_OK : BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_scan_stop(void);
extern "C" void bl_impl_ble_client_disconnect(int cl);

/* Stops everything and forgets the server's services, characteristics and
 * the remote characteristics. The controller stays on: the library has no
 * call to stop it. */
extern "C" void bl_impl_ble_deinit(void)
{
    if (!s_inited) {
        return;
    }
    bl_impl_ble_scan_stop();
    bl_impl_ble_client_disconnect(0);
    {
        BluetoothLock lock;
        gap_advertisements_enable(0);
        s_advertising = false;
        for (int i = 0; i < MAX_PERIPH; i++) {
            if (s_periph[i] != NO_CON) {
                gap_disconnect(s_periph[i]);
            }
        }
    }
    wait_until([] { return s_periph_count == 0; }, 1000);
    BluetoothLock lock;
    if (s_att_up) {
        BLE.stopAdvertising();      // also stops the ATT server
        s_att_up = false;
    }
    if (s_server) {
        ServerPeek::services(s_server).clear();
    }
    for (int h = 0; h < MAX_CHARS; h++) {
        delete s_chars[h];
        s_chars[h] = nullptr;
    }
    for (int h = 0; h < MAX_SERVICES; h++) {
        delete s_services[h];
        s_services[h] = nullptr;
        s_svc_started[h] = false;
    }
    delete s_nus_tx;
    delete s_nus_rx_char;
    delete s_nus_svc;
    s_nus_tx = s_nus_rx_char = nullptr;
    s_nus_svc = nullptr;
    s_nus_pending = false;
    s_nus_in.clear();
    s_nus_out.clear();
    if (s_remote) {
        for (int i = 0; i < MAX_REMOTE; i++) {
            if (s_remote[i].listening) {    // the disconnect did not finish in time
                gatt_client_stop_listening_for_characteristic_value_updates(&s_remote[i].notif);
            }
        }
        memset(s_remote, 0, sizeof(Remote) * MAX_REMOTE);
    }
    s_db_bytes = DB_BASE;
    s_db_dirty = true;
    s_inited = false;
}

extern "C" int bl_impl_ble_address(char *buf, size_t size)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    bd_addr_t a;
    {
        BluetoothLock lock;
        gap_local_bd_addr(a);
    }
    char text[18];
    addr_text(a, text);
    if (buf && size) {
        strlcpy(buf, text, size);
    }
    return (int)strlen(text);
}

extern "C" int bl_impl_ble_set_mtu(uint16_t mtu)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    if (mtu < 23) mtu = 23;
    if (mtu > 517) mtu = 517;           // the largest ATT MTU
    BluetoothLock lock;
    l2cap_set_max_le_mtu(mtu);          // offered in the next MTU exchange
    return BL_OK;
}

/* ---- GATT server ---- */

extern "C" int bl_impl_ble_server_create(void)
{
    return ensure_server() ? 0 : BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_server_connected_count(void)
{
    return s_periph_count;
}

extern "C" void bl_impl_ble_server_disconnect(uint16_t conn_id)
{
    if (s_stack_up) {
        BluetoothLock lock;
        gap_disconnect(conn_id);
    }
}

extern "C" int bl_impl_ble_service_create(const char *uuid, int num_handles)
{
    (void)num_handles;                  // BTstack's database has no per-service reservation
    Uuid u;
    if (!parse_uuid(uuid, u)) {
        return BL_ERR_ARG;
    }
    if (!ensure_server() || s_db_bytes + db_service_bytes(u) > DB_LIMIT) {
        return BL_ERR_NO_MEM;
    }
    for (int h = 0; h < MAX_SERVICES; h++) {
        if (!s_services[h]) {
            BLEService *s = new (std::nothrow) BLEService(lib_uuid(u));
            if (!s) {
                return BL_ERR_NO_MEM;
            }
            s_services[h] = s;
            s_svc_started[h] = false;
            s_db_bytes += db_service_bytes(u);
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_service_start(int svc)
{
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc]) {
        return BL_ERR_ARG;
    }
    BluetoothLock lock;
    if (!s_svc_started[svc]) {
        s_server->addService(s_services[svc]);
        s_svc_started[svc] = true;
        s_db_dirty = true;
    }
    if (s_advertising && s_periph_count == 0) {
        advertise();                    // rebuilds the database with the new service
    }
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
        push_data(BL_BLE_EV_WRITE, char_handle(c), (const uint8_t *)c->valueData(), c->valueLen());
    }
    void onRead(BLECharacteristic *c) override
    {
        push_simple(BL_BLE_EV_READ, char_handle(c));    // sent just before the value is answered
    }
};
static CharCallbacks s_char_cb;

static uint16_t to_att_props(uint32_t p)
{
    uint16_t r = 0;
    if (p & BL_BLE_PROP_READ) r |= ATT_PROPERTY_READ;
    if (p & BL_BLE_PROP_WRITE) r |= ATT_PROPERTY_WRITE;
    if (p & BL_BLE_PROP_NOTIFY) r |= ATT_PROPERTY_NOTIFY;
    if (p & BL_BLE_PROP_INDICATE) r |= ATT_PROPERTY_INDICATE;
    if (p & BL_BLE_PROP_WRITE_NR) r |= ATT_PROPERTY_WRITE_WITHOUT_RESPONSE;
    if (p & BL_BLE_PROP_BROADCAST) r |= ATT_PROPERTY_BROADCAST;
    return r;
}

extern "C" int bl_impl_ble_char_create(int svc, const char *uuid, uint32_t properties)
{
    Uuid u;
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc] || !parse_uuid(uuid, u)) {
        return BL_ERR_ARG;
    }
    uint16_t props = to_att_props(properties);
    int need = db_char_bytes(u, props);
    if (s_db_bytes + need > DB_LIMIT) {
        return BL_ERR_NO_MEM;           // BTstack's attribute database is full
    }
    for (int h = 0; h < MAX_CHARS; h++) {
        if (!s_chars[h]) {
            BlChar *c = new (std::nothrow) BlChar(lib_uuid(u), props);
            if (!c) {
                return BL_ERR_NO_MEM;
            }
            c->setCallbacks(&s_char_cb);
            BluetoothLock lock;
            s_services[svc]->addCharacteristic(c);
            s_chars[h] = c;
            s_db_bytes += need;
            s_db_dirty = true;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

static BlChar *char_of(int h)
{
    return (h >= 0 && h < MAX_CHARS) ? s_chars[h] : NULL;
}

extern "C" int bl_impl_ble_char_set_value(int ch, const uint8_t *data, size_t len)
{
    BlChar *c = char_of(ch);
    if (!c || (!data && len)) {
        return BL_ERR_ARG;
    }
    BluetoothLock lock;     // a central's write changes the value from BTstack's side
    return c->set_raw(data, len) ? BL_OK : BL_ERR_NO_MEM;
}

extern "C" int bl_impl_ble_char_get_value(int ch, uint8_t *buf, size_t size)
{
    BlChar *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    BluetoothLock lock;
    size_t len = c->len();
    if (buf && size && len) {
        memcpy(buf, c->data(), len < size ? len : size);
    }
    return (int)len;
}

static void char_can_send(void *ctx)
{
    BlChar *c = static_cast<BlChar *>(ctx);
    c->send_pending = false;
    if (c->send_con == NO_CON || c->send_con != first_periph()) {
        return;
    }
    if (c->send_indicate) {
        att_server_indicate(c->send_con, c->value_handle(), c->snap, c->snap_len);
    } else {
        att_server_notify(c->send_con, c->value_handle(), c->snap, c->snap_len);
    }
}

/* Sends the value to the central if it subscribed (CCCD bit 1 notifications,
 * bit 2 indications). One send waits per characteristic: a newer call waits
 * up to 200 ms for it, then replaces it. */
extern "C" int bl_impl_ble_char_notify(int ch, int indicate)
{
    BlChar *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    if (first_periph() == NO_CON) {
        return BL_OK;
    }
    wait_until([c] { return !c->send_pending; }, 200);
    BluetoothLock lock;
    uint16_t con = first_periph();
    if (con == NO_CON || !(c->cccd() & (indicate ? 2 : 1))) {
        return BL_OK;                   // nobody subscribed
    }
    uint16_t mtu = att_server_get_mtu(con);
    if (!c->snapshot(mtu > 3 ? mtu - 3 : 20)) {
        return BL_ERR_NO_MEM;
    }
    c->send_indicate = indicate != 0;
    c->send_con = con;
    // Directly when the controller has room: BTstack answers a send request only
    // while it processes radio events (see nus_drain()).
    if (!c->send_pending && att_server_can_send_packet_now(con)) {
        uint8_t st = indicate ? att_server_indicate(con, c->value_handle(), c->snap, c->snap_len)
                              : att_server_notify(con, c->value_handle(), c->snap, c->snap_len);
        if (st == ERROR_CODE_SUCCESS) {
            return BL_OK;
        }
    }
    if (!c->send_pending) {
        c->send_reg.callback = char_can_send;
        c->send_reg.context = c;
        uint8_t st = indicate ? att_server_request_to_send_indication(&c->send_reg, con)
                              : att_server_request_to_send_notification(&c->send_reg, con);
        if (st != ERROR_CODE_SUCCESS) {
            return BL_ERR_FAIL;
        }
        c->send_pending = true;
    }
    return BL_OK;
}

/* The library adds the CCCD (2902) itself and can hold a user description
 * (2901); other descriptors are not in its attribute database. */
extern "C" int bl_impl_ble_char_add_descriptor(int ch, const char *uuid, const uint8_t *value, size_t len)
{
    BlChar *c = char_of(ch);
    Uuid u;
    if (!c || !parse_uuid(uuid, u) || (!value && len)) {
        return BL_ERR_ARG;
    }
    if (u.is16 && u.u16 == 0x2902) {
        return BL_OK;
    }
    if (u.is16 && u.u16 == 0x2901) {
        int need = c->has_description() ? 0 : 8;
        if (s_db_bytes + need > DB_LIMIT) {
            return BL_ERR_NO_MEM;
        }
        BluetoothLock lock;
        if (!c->set_description(value, len)) {
            return BL_ERR_NO_MEM;
        }
        s_db_bytes += need;
        s_db_dirty = true;
        return BL_OK;
    }
    return BL_ERR_UNSUPPORTED;
}

/* ---- advertising ---- */

extern "C" int bl_impl_ble_adv_config(int what, const char *text, const uint8_t *data, size_t value)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    switch (what) {
    case BL_BLE_ADV_SERVICE_UUID: {
        Uuid u;
        if (!parse_uuid(text, u)) return BL_ERR_ARG;
        adv_add_uuid(u);
        break;
    }
    case BL_BLE_ADV_SCAN_RESPONSE: s_scan_resp = value != 0; break;
    case BL_BLE_ADV_NAME:
        if (!text) return BL_ERR_ARG;
        strlcpy(s_adv_name, text, sizeof(s_adv_name));
        break;
    case BL_BLE_ADV_MANUFACTURER:
        if (!data) return BL_ERR_ARG;
        s_manuf_len = (uint8_t)(value < sizeof(s_manuf) ? value : sizeof(s_manuf));
        memcpy(s_manuf, data, s_manuf_len);
        break;
    case BL_BLE_ADV_APPEARANCE:    s_appearance = (uint16_t)value; break;
    case BL_BLE_ADV_MIN_PREFERRED: s_pref_min = (uint16_t)value; break;
    case BL_BLE_ADV_MAX_PREFERRED: s_pref_max = (uint16_t)value; break;
    case BL_BLE_ADV_MIN_INTERVAL:  s_adv_min = (uint16_t)value; break;
    case BL_BLE_ADV_MAX_INTERVAL:  s_adv_max = (uint16_t)value; break;
    case BL_BLE_ADV_RESET:         adv_defaults(); break;
    default:                       return BL_ERR_ARG;
    }
    if (s_advertising) {
        BluetoothLock lock;
        apply_adv();
    }
    return BL_OK;
}

extern "C" int bl_impl_ble_adv_start(void)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    BluetoothLock lock;
    advertise();
    return BL_OK;
}

extern "C" int bl_impl_ble_adv_stop(void)
{
    if (!s_inited) {
        return BL_ERR_STATE;
    }
    BluetoothLock lock;
    gap_advertisements_enable(0);       // connections and the GATT server stay up
    s_advertising = false;
    return BL_OK;
}

/* ---- scanning ---- */

static void scan_timeout(btstack_timer_source_t *)
{
    gap_stop_scan();
    s_scanning = false;
    push_simple(BL_BLE_EV_SCAN_DONE, -1);
}

static uint16_t scan_units(uint32_t ms)
{
    uint32_t u = ms * 8 / 5;            // 0.625 ms units
    return (uint16_t)(u < 4 ? 4 : u > 0x400 ? 0x400 : u);
}

extern "C" int bl_impl_ble_scan_start(uint32_t seconds, int active, uint16_t interval_ms, uint16_t window_ms)
{
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    if (!s_results) {
        s_results = (bl_ble_adv_t *)malloc(sizeof(bl_ble_adv_t) * MAX_RESULTS);
        if (!s_results) {
            return BL_ERR_NO_MEM;
        }
    }
    uint16_t itv = scan_units(interval_ms ? interval_ms : 100);
    uint16_t win = scan_units(window_ms ? window_ms : 99);
    if (win > itv) {
        win = itv;
    }
    {
        IrqOff off;
        s_result_count = 0;
    }
    BluetoothLock lock;
    btstack_run_loop_remove_timer(&s_scan_timer);
    // The controller reports each device once; an active scan adds its scan response.
    gap_set_scan_duplicate_filter(true);
    if (gap_set_scan_params(active ? 1 : 0, itv, win, 0) != ERROR_CODE_SUCCESS) {
        return BL_ERR_ARG;
    }
    gap_start_scan();
    s_scanning = true;
    btstack_run_loop_set_timer_handler(&s_scan_timer, scan_timeout);
    btstack_run_loop_set_timer(&s_scan_timer, (seconds ? seconds : 5) * 1000);
    btstack_run_loop_add_timer(&s_scan_timer);
    return BL_OK;
}

extern "C" int bl_impl_ble_scan_stop(void)
{
    if (s_stack_up) {
        BluetoothLock lock;
        btstack_run_loop_remove_timer(&s_scan_timer);
        if (s_scanning) {
            gap_stop_scan();
        }
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
    IrqOff off;
    return s_result_count;
}

extern "C" int bl_impl_ble_scan_result(int index, bl_ble_adv_t *dev)
{
    if (!dev || index < 0) {
        return BL_ERR_ARG;
    }
    IrqOff off;
    if (index >= s_result_count) {
        return BL_ERR_ARG;
    }
    *dev = s_results[index];
    return BL_OK;
}

extern "C" void bl_impl_ble_scan_clear(void)
{
    IrqOff off;
    s_result_count = 0;
}

/* ---- client ----
 * One client connection (handle 0), on BTstack's GATT client. Services and
 * characteristics are found when the sketch asks for one, as on the ESP32. */

/* Runs one GATT request: start(con) starts it (retried while BTstack's
 * client is busy, e.g. with the MTU exchange after connecting), then waits
 * for its completion. BL_OK means it completed: s_op_status has the ATT
 * status. */
template <typename F> static int gatt_op(uint8_t op, F start)
{
    if (!s_op_done) {
        return BL_ERR_STATE;            // an earlier request has not finished
    }
    uint32_t t0 = millis();
    for (;;) {
        uint8_t st;
        {
            BluetoothLock lock;
            if (s_client_con == NO_CON) {
                return BL_ERR_STATE;
            }
            s_op = op;
            s_op_done = false;
            st = start((uint16_t)s_client_con);
            if (st != ERROR_CODE_SUCCESS) {
                s_op = OP_NONE;
                s_op_done = true;
            }
        }
        if (st == ERROR_CODE_SUCCESS) {
            break;
        }
        if ((st != GATT_CLIENT_IN_WRONG_STATE && st != GATT_CLIENT_BUSY) || millis() - t0 > 2000) {
            return BL_ERR_FAIL;
        }
        delay(5);
    }
    wait_until([] { return (bool)s_op_done; }, OP_TIMEOUT_MS);
    BluetoothLock lock;
    if (!s_op_done) {
        s_read_buf = nullptr;           // a late answer must not write into the caller's buffer
        return BL_ERR_TIMEOUT;
    }
    return BL_OK;
}

static Remote *remote_of(int h)
{
    return (s_remote && h >= 0 && h < MAX_REMOTE && s_remote[h].used) ? &s_remote[h] : NULL;
}

static bool remote_live(const Remote *r)
{
    return r->con != NO_CON && r->con == s_client_con;
}

extern "C" int bl_impl_ble_client_connect(const char *address, int addr_type)
{
    bd_addr_t a;
    if (!address || !sscanf_bd_addr(address, a)) {
        return BL_ERR_ARG;
    }
    if (!ensure_init(NULL)) {
        return BL_ERR_NO_MEM;
    }
    if (s_client_con != NO_CON || s_client_connecting) {
        return BL_ERR_NO_MEM;           // BTstack is set up for one client connection
    }
    BlWdogLong wdt;
    uint8_t st;
    {
        BluetoothLock lock;
        s_client_failed = false;
        s_client_connecting = true;
        st = gap_connect(a, (bd_addr_type_t)(addr_type >= 0 && addr_type <= 3 ? addr_type : 0));
        if (st != ERROR_CODE_SUCCESS) {
            s_client_connecting = false;
        }
    }
    if (st != ERROR_CODE_SUCCESS) {
        return BL_ERR_FAIL;
    }
    if (!wait_until([] { return !s_client_connecting; }, CONNECT_MS)) {
        {
            BluetoothLock lock;
            gap_connect_cancel();
        }
        wait_until([] { return !s_client_connecting; }, 1000);
        BluetoothLock lock;
        s_client_connecting = false;
    }
    return s_client_con != NO_CON ? 0 : BL_ERR_FAIL;
}

extern "C" void bl_impl_ble_client_disconnect(int cl)
{
    if (cl != 0 || s_client_con == NO_CON) {
        return;
    }
    {
        BluetoothLock lock;
        gap_disconnect(s_client_con);
    }
    wait_until([] { return s_client_con == NO_CON; }, 2000);
}

extern "C" int bl_impl_ble_client_connected(int cl)
{
    return (cl == 0 && s_client_con != NO_CON) ? 1 : 0;
}

extern "C" int bl_impl_ble_client_rssi(int cl)
{
    if (cl != 0 || s_client_con == NO_CON) {
        return 0;
    }
    {
        BluetoothLock lock;
        s_rssi_done = false;
        if (gap_read_rssi(s_client_con) != 0) {
            return 0;
        }
    }
    return wait_until([] { return (bool)s_rssi_done; }, 500) ? s_rssi : 0;
}

/* Finds the service, then the characteristic in it, on the connected peripheral. */
static bool discover(Remote &r)
{
    s_found_svc = false;
    int rc = gatt_op(OP_SERVICE, [&r](uint16_t con) {
        return r.svc.is16 ? gatt_client_discover_primary_services_by_uuid16(gatt_handler, con, r.svc.u16)
                          : gatt_client_discover_primary_services_by_uuid128(gatt_handler, con, r.svc.u128);
    });
    if (rc != BL_OK || !s_found_svc) {
        return false;
    }
    s_found_chr = false;
    rc = gatt_op(OP_CHAR, [&r](uint16_t con) {
        return r.chr.is16 ? gatt_client_discover_characteristics_for_service_by_uuid16(gatt_handler, con, &s_found_service, r.chr.u16)
                          : gatt_client_discover_characteristics_for_service_by_uuid128(gatt_handler, con, &s_found_service, r.chr.u128);
    });
    if (rc != BL_OK || !s_found_chr) {
        return false;
    }
    BluetoothLock lock;
    r.ch = s_found_char;
    r.con = s_client_con;
    return r.con != NO_CON;
}

extern "C" int bl_impl_ble_remote_char(int cl, const char *service_uuid, const char *char_uuid)
{
    Uuid su, cu;
    if (cl != 0 || !parse_uuid(service_uuid, su) || !parse_uuid(char_uuid, cu)) {
        return BL_ERR_ARG;
    }
    if (s_client_con == NO_CON) {
        return BL_ERR_STATE;
    }
    if (!s_remote) {
        s_remote = (Remote *)calloc(MAX_REMOTE, sizeof(Remote));
        if (!s_remote) {
            return BL_ERR_NO_MEM;
        }
    }
    // The same characteristic keeps its handle, found again after a reconnect.
    int h = -1;
    for (int i = 0; i < MAX_REMOTE && h < 0; i++) {
        if (s_remote[i].used && same_uuid(s_remote[i].svc, su) && same_uuid(s_remote[i].chr, cu)) {
            h = i;
        }
    }
    if (h >= 0 && remote_live(&s_remote[h])) {
        return h;
    }
    bool fresh = h < 0;
    for (int i = 0; i < MAX_REMOTE && h < 0; i++) {
        if (!s_remote[i].used) {
            h = i;
        }
    }
    if (h < 0) {
        return BL_ERR_NO_MEM;
    }
    Remote &r = s_remote[h];
    if (fresh) {
        BluetoothLock lock;
        memset(&r, 0, sizeof(r));
        r.svc = su;
        r.chr = cu;
        r.con = NO_CON;
    }
    BlWdogLong wdt;     // service discovery talks to the peripheral
    if (!discover(r)) {
        if (fresh) {
            BluetoothLock lock;
            r.used = false;
        }
        return BL_ERR_FAIL;
    }
    BluetoothLock lock;
    r.used = true;
    return h;
}

/* Reads up to one ATT packet of the value (MTU - 1 bytes, 244 or more once the
 * MTU exchange is done). Returns the length read. */
extern "C" int bl_impl_ble_remote_read(int rc, uint8_t *buf, size_t size)
{
    Remote *r = remote_of(rc);
    if (!r) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    if (!s_op_done) {
        return BL_ERR_STATE;
    }
    BlWdogLong wdt;
    {
        BluetoothLock lock;
        s_read_buf = buf;
        s_read_size = buf ? size : 0;
        s_read_len = 0;
    }
    uint16_t vh = r->ch.value_handle;
    int st = gatt_op(OP_READ, [vh](uint16_t con) {
        return gatt_client_read_value_of_characteristic_using_value_handle(gatt_handler, con, vh);
    });
    {
        BluetoothLock lock;
        s_read_buf = nullptr;
    }
    if (st != BL_OK) {
        return st;
    }
    return s_op_status == ATT_ERROR_SUCCESS ? (int)s_read_len : BL_ERR_FAIL;
}

extern "C" int bl_impl_ble_remote_write(int rc, const uint8_t *data, size_t len, int response)
{
    Remote *r = remote_of(rc);
    if (!r || !data || len > 512) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    uint16_t con = s_client_con, vh = r->ch.value_handle, mtu = ATT_DEFAULT_MTU;
    {
        BluetoothLock lock;
        gatt_client_get_mtu(con, &mtu);
    }
    if (!response) {
        if (len > (size_t)(mtu - 3)) {
            return BL_ERR_ARG;          // a write without response is one packet
        }
        uint32_t t0 = millis();
        for (;;) {
            uint8_t st;
            {
                BluetoothLock lock;
                st = gatt_client_write_value_of_characteristic_without_response(con, vh, (uint16_t)len, (uint8_t *)data);
            }
            if (st == ERROR_CODE_SUCCESS) {
                return BL_OK;
            }
            if ((st != GATT_CLIENT_BUSY && st != BTSTACK_ACL_BUFFERS_FULL && st != GATT_CLIENT_IN_WRONG_STATE) ||
                millis() - t0 > 1000) {
                return BL_ERR_FAIL;
            }
            delay(2);
        }
    }
    if (!s_op_done) {
        return BL_ERR_STATE;
    }
    // BTstack reads the value while the request runs: keep a copy of our own.
    free(s_write_buf);
    s_write_buf = (uint8_t *)malloc(len ? len : 1);
    if (!s_write_buf) {
        return BL_ERR_NO_MEM;
    }
    memcpy(s_write_buf, data, len);
    BlWdogLong wdt;
    bool is_long = len > (size_t)(mtu - 3);
    int st = gatt_op(OP_WRITE, [vh, len, is_long](uint16_t c) {
        return is_long ? gatt_client_write_long_value_of_characteristic(gatt_handler, c, vh, (uint16_t)len, s_write_buf)
                       : gatt_client_write_value_of_characteristic(gatt_handler, c, vh, (uint16_t)len, s_write_buf);
    });
    if (st != BL_OK) {
        return st;
    }
    return s_op_status == ATT_ERROR_SUCCESS ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_ble_remote_subscribe(int rc, int mode)
{
    Remote *r = remote_of(rc);
    if (!r || mode < 0 || mode > 2) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    uint16_t cfg = mode == 0 ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NONE
                 : mode == 1 ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION
                             : GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_INDICATION;
    BlWdogLong wdt;
    if (mode != 0) {
        BluetoothLock lock;     // listen first, so the first value after subscribing is not missed
        if (!r->listening) {
            gatt_client_listen_for_characteristic_value_updates(&r->notif, gatt_handler, r->con, &r->ch);
            r->listening = true;
        }
    }
    int st = gatt_op(OP_CCCD, [r, cfg](uint16_t con) {
        return gatt_client_write_client_characteristic_configuration(gatt_handler, con, &r->ch, cfg);
    });
    bool ok = st == BL_OK && s_op_status == ATT_ERROR_SUCCESS;
    if (mode == 0 || !ok) {
        BluetoothLock lock;
        if (r->listening) {
            gatt_client_stop_listening_for_characteristic_value_updates(&r->notif);
            r->listening = false;
        }
    }
    if (st != BL_OK) {
        return st;
    }
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_ble_remote_props(int rc)
{
    Remote *r = remote_of(rc);
    if (!r) {
        return 0;
    }
    uint16_t p = r->ch.properties;
    uint32_t b = 0;
    if (p & ATT_PROPERTY_READ) b |= BL_BLE_PROP_READ;
    if (p & ATT_PROPERTY_WRITE) b |= BL_BLE_PROP_WRITE;
    if (p & ATT_PROPERTY_NOTIFY) b |= BL_BLE_PROP_NOTIFY;
    if (p & ATT_PROPERTY_INDICATE) b |= BL_BLE_PROP_INDICATE;
    if (p & ATT_PROPERTY_WRITE_WITHOUT_RESPONSE) b |= BL_BLE_PROP_WRITE_NR;
    if (p & ATT_PROPERTY_BROADCAST) b |= BL_BLE_PROP_BROADCAST;
    return b;
}

/* ---- events ---- */

extern "C" int bl_impl_ble_event_take(bl_ble_event_t *ev)
{
    if (!ev || !s_ev) {
        return 0;
    }
    IrqOff off;
    if (!s_ev_count) {
        return 0;
    }
    *ev = s_ev[s_ev_head];
    s_ev_head = (uint8_t)((s_ev_head + 1) % EVENT_QUEUE_LEN);
    s_ev_count--;
    return 1;
}

#endif /* BL_HAS_BLE */
