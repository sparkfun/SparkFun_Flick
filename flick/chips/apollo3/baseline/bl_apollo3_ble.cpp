/*
 * bl_apollo3_ble.cpp - Bluetooth LE on the Apollo3 (SparkFun Artemis), on
 * ArduinoBLE over the Apollo3 core's Cordio HCI driver for the chip's own BLE
 * controller. The same table entries as the ESP32 Baselines
 * (baseline/src/bl_impl_ble.cpp): the sketch's BLEDevice, BLEServer, BLEScan,
 * BLEClient, ... and BLE serial (the Nordic UART Service) wrap these by handle.
 *
 * What comes from where:
 *  - ArduinoBLE: the HCI host, ATT/GATT server (its attribute database and
 *    the answers to reads and writes), connections, and the GATT client's
 *    discovery and requests;
 *  - this file, with HCI commands and ATT packets of its own where the
 *    library has no call for it: advertising and scan response data, scan
 *    parameters and reports, notifications and indications (only to centrals,
 *    with time limits), and values set without being sent.
 *
 * Built with FAKE_GAP and FAKE_ATT (flick's Apollo3 builder): the library then
 * leaves out its GAP and ATT objects, and this file supplies them, as
 * subclasses that see every advertising report and connection, and keep a
 * central's write from being sent back to it as a notification (the library's
 * value setter notifies). ArduinoBLE's headers are included with their
 * private members opened (below): the library keeps the value length, the
 * CCCD and the connection table private, and this file needs them.
 *
 * Context: everything runs on the sketch's thread. The library's own thread
 * ("bleLoop", Cordio's WSF dispatcher) only reads HCI packets from the
 * controller; they land in a 4 KB buffer here (the library's own holds 256
 * bytes and drops a packet that doesn't fit) and are fed to the library's
 * input as it has room, from that thread, a 5 ms Ticker and bl_poll(). The
 * library parses them, and calls back, only inside HCI.poll(), which
 * bl_poll() runs on the sketch's thread (delay(), yield(), between loop()
 * calls). Its callbacks only queue bl_ble_event_t records, which the sketch
 * takes with ble_event_take() (the SDK's pump). Entries that wait for the
 * radio or the peer (start-up, connect, discovery, reads, writes with
 * response) block the sketch but not Baseline: see on_worker().
 *
 * Limits of this port: three connections in all (Mbed's DM_CONN_MAX), one
 * of them a GATT client connection; centrals share one CCCD
 * per characteristic (ArduinoBLE keeps one); characteristic values up to 512
 * bytes; services and their characteristics and descriptors are fixed once
 * the service is started; a remote read returns one ATT packet (MTU - 1
 * bytes) and a remote write is one packet (MTU - 3 bytes); the radio stays on
 * after ble_deinit() (a restart turns it off).
 */
#include "bl_apollo3.h"

#if defined(BL_HAS_BLE)

#include <new>
#include <string.h>
#include <mbed.h>

/* ArduinoBLE sizes its connection table (ATT_MAX_PEERS) by Mbed's DM_CONN_MAX
 * (mbed_config.h: 3), which every file of the build sees through Arduino.h:
 * this file's view of the table matches the library's. */
#if !defined(DM_CONN_MAX)
#error "DM_CONN_MAX is not defined here: ArduinoBLE's connection table would differ from the library's"
#endif

#define private public
#define protected public
#include <ArduinoBLE.h>
#include <local/BLELocalDescriptor.h>
#include <remote/BLERemoteDevice.h>
#include <remote/BLERemoteService.h>
#include <remote/BLERemoteCharacteristic.h>
#include <remote/BLERemoteDescriptor.h>
#include <utility/ATT.h>
#include <utility/GAP.h>
#include <utility/GATT.h>
#include <utility/HCI.h>
#include <utility/HCICordioTransport.h>
#include <driver/CordioHCITransportDriver.h>
#undef private
#undef protected

#if !defined(FAKE_GAP) || !defined(FAKE_ATT)
#error "Baseline's BLE needs FAKE_GAP and FAKE_ATT (flick's Apollo3 builder sets them)"
#endif

extern HCICordioTransportClass HCICordioTransport;     // HCICordioTransport.cpp

#if (MBED_VERSION > MBED_ENCODE_VERSION(6, 2, 0))
using CordioTransport = ble::CordioHCITransportDriver;
#else
using CordioTransport = ble::vendor::cordio::CordioHCITransportDriver;
#endif

#define NUS_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   // central writes here
#define NUS_TX      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   // we notify here
#define NUS_BUFFER  1024

#define EVENT_QUEUE_LEN 16
#define MAX_SERVICES    12
#define MAX_CHARS       32
#define MAX_DESCS       16
#define MAX_RESULTS     40
#define MAX_REMOTE      16
#define MAX_ADV_UUIDS   4
#define VALUE_MAX       512         // the largest attribute value ATT allows
#define HCI_RX_SIZE     4096
#define NO_CON          0xFFFF
#define ROLE_CENTRAL    0x00        // HCI role byte: we are the central
#define ROLE_PERIPHERAL 0x01        // we are the peripheral (the GATT server's side)

#define ATT_OP_NOTIFY   0x1B
#define ATT_OP_IND      0x1D

/* ---- helpers ---- */

/* Waits until done() or ms have passed; Baseline (the loader, BLE) is polled meanwhile. */
template <typename F> static bool wait_until(F done, uint32_t ms)
{
    uint32_t start = millis();
    while (!done()) {
        if (millis() - start >= ms) {
            return false;
        }
        bl_poll();
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/* Library calls that wait for the controller or the peer (starting the
 * radio takes about 3 s, a connection or a GATT request up to 5 s) run on a
 * thread of their own while the sketch's thread keeps polling Baseline, so
 * an upload or INFO arriving meanwhile is answered (the loader only runs in
 * bl_poll()). bl_ble_poll() leaves HCI alone while one runs: the library
 * isn't thread-safe, and the call polls HCI itself. Library callbacks then
 * run on that thread; the sketch's thread only waits. */
static volatile bool s_worker_busy;       // also: entries that use the library refuse (BL_ERR_STATE),
                                          // e.g. from a Ticker callback that bl_poll() runs meanwhile

template <typename F> struct WorkerJob {
    F *fn;
    volatile bool done;
    static void run(WorkerJob *j)
    {
        (*j->fn)();
        j->done = true;
    }
};

template <typename F> static void on_worker(F fn)
{
    WorkerJob<F> job = { &fn, false };
    rtos::Thread t(osPriorityNormal, 6144, nullptr, "ble");
    s_worker_busy = true;
    if (t.start(mbed::callback(&WorkerJob<F>::run, &job)) != osOK) {
        fn();                       // no memory for the thread: run it here (the loader waits)
        s_worker_busy = false;
        return;
    }
    while (!job.done) {
        bl_poll();
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(2));
    }
    t.join();
    s_worker_busy = false;
}

/* A UUID as text: "180d", "0000180d", "0x180D" or the 36-character form.
 * Base UUIDs (xxxxxxxx-0000-1000-8000-00805f9b34fb with a 16-bit value) are
 * kept as 16-bit ones, as peers expect. u128 is in text order (big-endian). */
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

static bool same_uuid(const Uuid &a, const Uuid &b)
{
    return a.is16 == b.is16 && (a.is16 ? a.u16 == b.u16 : memcmp(a.u128, b.u128, 16) == 0);
}

/* Hex text without printf: Mbed's printf (which the core links in place of
 * newlib's) ignores zero padding, so "%02x" would drop leading zeros. */
static const char HEX_DIGITS[] = "0123456789abcdef";

static char *put_hex(char *p, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        *p++ = HEX_DIGITS[b[i] >> 4];
        *p++ = HEX_DIGITS[b[i] & 15];
    }
    return p;
}

static void uuid128_text(const uint8_t *u, char out[37])
{
    char *p = put_hex(out, u, 4);
    *p++ = '-';
    p = put_hex(p, u + 4, 2);
    *p++ = '-';
    p = put_hex(p, u + 6, 2);
    *p++ = '-';
    p = put_hex(p, u + 8, 2);
    *p++ = '-';
    p = put_hex(p, u + 10, 6);
    *p = 0;
}

/* ArduinoBLE's form: "180d" for a 16-bit UUID, else the 36-character one, lower case. */
static void lib_text(const Uuid &u, char out[37])
{
    if (u.is16) {
        uint8_t b[2] = { (uint8_t)(u.u16 >> 8), (uint8_t)u.u16 };
        *put_hex(out, b, 2) = 0;
    } else {
        uuid128_text(u.u128, out);
    }
}

static void uuid32_text(uint32_t v, char out[37])
{
    uint8_t u[16];
    memcpy(u, BASE_UUID, 16);
    u[0] = (uint8_t)(v >> 24);
    u[1] = (uint8_t)(v >> 16);
    u[2] = (uint8_t)(v >> 8);
    u[3] = (uint8_t)v;
    uuid128_text(u, out);
}

/* HCI addresses are little-endian; the text is most significant byte first. */
static void addr_text(const uint8_t a[6], char out[18])
{
    char *p = out;
    for (int i = 5; i >= 0; i--) {
        p = put_hex(p, a + i, 1);
        *p++ = i ? ':' : 0;
    }
}

static bool parse_addr(const char *s, uint8_t a[6])
{
    for (int i = 5; i >= 0; i--) {
        int hi = hex_value(s[0]), lo = hi >= 0 ? hex_value(s[1]) : -1;
        if (hi < 0 || lo < 0) {
            return false;
        }
        a[i] = (uint8_t)(hi << 4 | lo);
        s += 2;
        if (i && *s++ != ':') {
            return false;
        }
    }
    return *s == '\0';
}

/* A byte FIFO, used on the sketch's thread only. */
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
        size_t k = 0;
        while (k < n && count < size) {
            buf[(head + count) % size] = d[k++];
            count++;
        }
        return k;
    }
    int get()
    {
        if (!count) {
            return -1;
        }
        int c = buf[head];
        head = (uint16_t)((head + 1) % size);
        count--;
        return c;
    }
    void clear() { head = count = 0; }
};

/* ---- HCI input --------------------------------------------------------------
 * Packets from the controller arrive on the library's thread. They go into the
 * library's input buffer (256 bytes) while it has room and nothing waits here;
 * the rest waits here until it has room again. The library reads its buffer
 * with interrupts off, so moving bytes with interrupts off keeps both sides
 * apart. */
static uint8_t *s_hci_rx;
static volatile uint16_t s_hci_head, s_hci_count;
static volatile uint32_t s_hci_dropped;
static mbed::Ticker s_hci_ticker;

static void hci_move_locked(void)
{
    auto &rb = HCICordioTransport._rxBuf;
    while (s_hci_count && rb.availableForStore() > 0) {
        rb.store_char(s_hci_rx[s_hci_head]);
        s_hci_head = (uint16_t)((s_hci_head + 1) % HCI_RX_SIZE);
        s_hci_count--;
    }
}

static void hci_move(void)
{
    if (s_hci_count) {
        mbed::CriticalSectionLock lock;
        hci_move_locked();
    }
}

static void hci_received(uint8_t *data, uint8_t len)
{
    mbed::CriticalSectionLock lock;
    hci_move_locked();
    auto &rb = HCICordioTransport._rxBuf;
    uint8_t k = 0;
    if (!s_hci_count) {
        while (k < len && rb.availableForStore() > 0) {
            rb.store_char(data[k++]);
        }
    }
    if (HCI_RX_SIZE - s_hci_count < (uint32_t)(len - k)) {
        s_hci_dropped++;            // cannot happen unless the sketch's thread stops polling
        return;
    }
    while (k < len) {
        s_hci_rx[(s_hci_head + s_hci_count) % HCI_RX_SIZE] = data[k++];
        s_hci_count++;
    }
}

/* ---- events ---- */

static bl_ble_event_t *s_ev;
static uint8_t s_ev_head, s_ev_count;

static void push_event(const bl_ble_event_t &ev)
{
    if (!s_ev) {
        return;
    }
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

/* ---- shared state ---- */

static bool s_stack_up;             // BLE.begin() has run; the radio stays on until a restart
static bool s_inited;
static char s_name[30];
static uint16_t s_mtu_limit = 23;   // the largest MTU the controller's buffers allow

/* Server objects. ArduinoBLE keeps a UUID's text by pointer, so each keeps its own. */
struct Svc {
    BLELocalService *obj;
    char uuid[37];
    bool started;
};
struct Chr {
    BLELocalCharacteristic *obj;
    char uuid[37];
    int svc;
};
struct Desc {
    BLELocalDescriptor *obj;
    char uuid[37];
    uint8_t *value;
};
static Svc s_services[MAX_SERVICES];
static Chr s_chars[MAX_CHARS];
static Desc s_descs[MAX_DESCS];
static bool s_advertising;

/* advertising settings (ble_adv_config) */
static Uuid s_adv_uuids[MAX_ADV_UUIDS];
static int s_adv_uuid_count;
static bool s_scan_resp = true;     // Arduino-ESP32's default: the name goes in the scan response
static char s_adv_name[30];
static uint8_t s_manuf[29];
static uint8_t s_manuf_len;
static uint16_t s_appearance;
static uint16_t s_pref_min, s_pref_max;
static uint16_t s_adv_min = 0x30, s_adv_max = 0x30;   // 0.625 ms units: 30 ms
static uint8_t s_adv_buf[31], s_adv_len;
static uint8_t s_sr_buf[31], s_sr_len;

/* BLE serial */
static BLELocalService *s_nus_svc;
static BLELocalCharacteristic *s_nus_tx, *s_nus_rx;
static Ring s_nus_in, s_nus_out;
static void nus_drain(void);

/* client */
struct Remote {
    bool used;
    bool listening;
    Uuid svc, chr;
    uint16_t con;                       // the connection it was found on; NO_CON once that is gone
    uint16_t value_handle, cccd_handle;
    uint8_t props;
};
static Remote *s_remote;                // MAX_REMOTE entries, allocated on first use
static uint16_t s_client_con = NO_CON;
static uint8_t s_client_type;
static uint8_t s_client_addr[6];

/* scanning */
static bl_ble_adv_t *s_results;         // MAX_RESULTS entries, allocated on the first scan
static int s_result_count;
static bool s_scanning;
static uint32_t s_scan_start, s_scan_ms;

/* ---- connections (ATT's table) ---- */

static int peer_role(uint16_t con)
{
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        if (ATT._peers[i].connectionHandle == con) {
            return ATT._peers[i].role;
        }
    }
    return -1;
}

static int central_count(void)
{
    int n = 0;
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        if (ATT._peers[i].connectionHandle != NO_CON && ATT._peers[i].role == ROLE_PERIPHERAL) {
            n++;
        }
    }
    return n;
}

static bool any_peer(void)
{
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        if (ATT._peers[i].connectionHandle != NO_CON) {
            return true;
        }
    }
    return false;
}

/* ---- scanning: GAP ---- */

static void scan_report(uint8_t type, uint8_t addr_type, const uint8_t addr[6], uint8_t len, const uint8_t *d, int8_t rssi)
{
    (void)type;
    if (!s_scanning || !s_results) {
        return;
    }
    bl_ble_adv_t r = {};
    addr_text(addr, r.address);
    r.address_type = addr_type;
    r.rssi = rssi;
    for (uint8_t i = 0; i + 1 < len;) {
        uint8_t n = d[i];                       // type byte + data
        if (n == 0 || i + 1 + n > len) {
            break;
        }
        uint8_t t = d[i + 1];
        const uint8_t *v = d + i + 2;
        uint8_t k = (uint8_t)(n - 1);
        switch (t) {
        case 0x08:                              // shortened local name
        case 0x09: {                            // complete local name
            size_t m = k < sizeof(r.name) - 1 ? k : sizeof(r.name) - 1;
            memcpy(r.name, v, m);
            r.name[m] = 0;
            r.has_name = 1;
            break;
        }
        case 0x02:                              // 16-bit service UUIDs
        case 0x03:
            if (!r.has_service_uuid && k >= 2) {
                uuid32_text((uint32_t)(v[0] | v[1] << 8), r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case 0x04:                              // 32-bit
        case 0x05:
            if (!r.has_service_uuid && k >= 4) {
                uuid32_text((uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24,
                            r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case 0x06:                              // 128-bit
        case 0x07:
            if (!r.has_service_uuid && k >= 16) {
                uint8_t be[16];
                for (int j = 0; j < 16; j++) {
                    be[j] = v[15 - j];
                }
                uuid128_text(be, r.service_uuid);
                r.has_service_uuid = 1;
            }
            break;
        case 0x0A:                              // TX power level
            if (k >= 1) {
                r.tx_power = (int8_t)v[0];
                r.has_tx_power = 1;
            }
            break;
        case 0x19:                              // appearance
            if (k >= 2) {
                r.appearance = (uint16_t)(v[0] | v[1] << 8);
            }
            break;
        case 0xFF:                              // manufacturer specific data
            r.manufacturer_len = (uint8_t)(k < sizeof(r.manufacturer_data) ? k : sizeof(r.manufacturer_data));
            memcpy(r.manufacturer_data, v, r.manufacturer_len);
            r.has_manufacturer_data = 1;
            break;
        default:
            break;
        }
        i = (uint8_t)(i + 1 + n);
    }
    // An advertisement and its scan response arrive as two reports: merge them.
    int index = -1;
    for (int i = 0; i < s_result_count; i++) {
        if (strcmp(s_results[i].address, r.address) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (s_result_count >= MAX_RESULTS) {
            return;
        }
        index = s_result_count++;
        s_results[index] = r;
        push_simple(BL_BLE_EV_SCAN_RESULT, -1, 0, index);  // once per device, as in Arduino
        return;
    }
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

class BlGap : public GAPClass {
public:
    void handleLeAdvertisingReport(uint8_t type, uint8_t addressType, uint8_t address[6], uint8_t eirLength,
                                   uint8_t eirData[], int8_t rssi) override
    {
        scan_report(type, addressType, address, eirLength, eirData, rssi);
    }
};
static BlGap s_gap;
GAPClass &GAP = s_gap;

/* ---- connections and incoming ATT: ATT ---- */

static void client_lost(void)
{
    s_client_con = NO_CON;
    if (s_remote) {
        for (int i = 0; i < MAX_REMOTE; i++) {
            s_remote[i].con = NO_CON;
            s_remote[i].listening = false;
        }
    }
    push_simple(BL_BLE_EV_CLIENT_DISCONNECT, 0);
}

static Chr *chr_of_value_handle(uint16_t h)
{
    for (int i = 0; i < MAX_CHARS; i++) {
        if (s_chars[i].obj && s_chars[i].obj->valueHandle() == h) {
            return &s_chars[i];
        }
    }
    if (s_nus_tx && s_nus_tx->valueHandle() == h) {
        static Chr nus;
        nus.obj = s_nus_tx;
        return &nus;
    }
    return NULL;
}

class BlAtt : public ATTClass {
public:
    void addConnection(uint16_t handle, uint8_t role, uint8_t peerBdaddrType, uint8_t peerBdaddr[6],
                       uint16_t interval, uint16_t latency, uint16_t supervisionTimeout,
                       uint8_t masterClockAccuracy) override
    {
        ATTClass::addConnection(handle, role, peerBdaddrType, peerBdaddr, interval, latency, supervisionTimeout,
                                masterClockAccuracy);
        if (peer_role(handle) < 0) {
            return;                 // the table was full: the library ignores the connection
        }
        if (role == ROLE_PERIPHERAL) {
            push_simple(BL_BLE_EV_CONNECT, 0, handle);
        } else if (s_client_con == NO_CON) {
            s_client_con = handle;
            push_simple(BL_BLE_EV_CLIENT_CONNECT, 0);
        }
    }

    void removeConnection(uint16_t handle, uint8_t reason) override
    {
        int role = peer_role(handle);
        ATTClass::removeConnection(handle, reason);
        // The controller drops a lost connection's unsent packets without
        // counting them as sent; the library would wait for them forever.
        if (!any_peer()) {
            HCI._pendingPkt = 0;
        }
        if (role == ROLE_PERIPHERAL) {
            push_simple(BL_BLE_EV_DISCONNECT, 0, handle);
        } else if (role == ROLE_CENTRAL && handle == s_client_con) {
            client_lost();
        }
    }

    /* A central's write: the library stores it with its value setter, which
     * also notifies a subscribed central. With the CCCD cleared meanwhile, it
     * only stores. */
    void writeReqOrCmd(uint16_t connectionHandle, uint16_t mtu, uint8_t op, uint8_t dlen, uint8_t data[]) override
    {
        Chr *c = NULL;
        if (dlen >= 2) {
            c = chr_of_value_handle((uint16_t)(data[0] | data[1] << 8));
        }
        uint16_t cccd = c ? c->obj->_cccdValue : 0;
        if (c) {
            c->obj->_cccdValue = 0;
        }
        ATTClass::writeReqOrCmd(connectionHandle, mtu, op, dlen, data);
        if (c) {
            c->obj->_cccdValue = cccd;
        }
    }

    void execWriteReq(uint16_t connectionHandle, uint16_t mtu, uint8_t dlen, uint8_t data[]) override
    {
        Chr *c = _longWriteHandle ? chr_of_value_handle((uint16_t)(_longWriteHandle + 1)) : NULL;
        uint16_t cccd = c ? c->obj->_cccdValue : 0;
        if (c) {
            c->obj->_cccdValue = 0;
        }
        ATTClass::execWriteReq(connectionHandle, mtu, dlen, data);
        if (c) {
            c->obj->_cccdValue = cccd;
        }
    }

    void handleNotifyOrInd(uint16_t connectionHandle, uint8_t opcode, uint8_t dlen, uint8_t data[]) override
    {
        if (dlen >= 2 && s_remote) {
            uint16_t vh = (uint16_t)(data[0] | data[1] << 8);
            for (int i = 0; i < MAX_REMOTE; i++) {
                Remote &r = s_remote[i];
                if (r.used && r.listening && r.con == connectionHandle && r.value_handle == vh) {
                    push_data(BL_BLE_EV_NOTIFY, i, data + 2, dlen - 2, opcode == ATT_OP_NOTIFY);
                    break;
                }
            }
        }
        ATTClass::handleNotifyOrInd(connectionHandle, opcode, dlen, data);     // confirms an indication
    }
};
static BlAtt s_att;
ATTClass &ATT = s_att;

/* ---- sending notifications and indications ---- */

/* Waits (up to ms) until the controller has a free ACL buffer: the library's
 * own send waits without a limit. */
static bool acl_room(uint32_t ms)
{
    return wait_until([] { return HCI._pendingPkt < HCI._maxPkt; }, ms);
}

/* Sends a value to every subscribed central: CCCD bit 1 notifications, bit 2
 * indications (each waits up to 2 s for the central's confirmation). Up to
 * MTU - 3 bytes per central. Returns how many centrals it went to. */
static int send_value(BLELocalCharacteristic *c, const uint8_t *data, size_t len, bool indicate)
{
    uint16_t vh = c->valueHandle();
    int sent = 0;
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        uint16_t con = ATT._peers[i].connectionHandle;
        if (con == NO_CON || ATT._peers[i].role != ROLE_PERIPHERAL) {
            continue;
        }
        uint16_t mtu = ATT._peers[i].mtu;
        size_t n = len < (size_t)(mtu - 3) ? len : (size_t)(mtu - 3);
        uint8_t pdu[3 + VALUE_MAX];
        pdu[0] = indicate ? ATT_OP_IND : ATT_OP_NOTIFY;
        pdu[1] = (uint8_t)vh;
        pdu[2] = (uint8_t)(vh >> 8);
        memcpy(pdu + 3, data, n);
        if (!acl_room(1000) || ATT._peers[i].connectionHandle != con) {
            continue;
        }
        ATT._cnf = false;
        HCI.sendAclPkt(con, ATT_CID, (uint8_t)(n + 3), pdu);
        if (indicate) {
            wait_until([con] { return ATT._cnf || peer_role(con) < 0; }, 2000);
        }
        sent++;
    }
    return sent;
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

/* Start BLE once. The name only counts on the first call (and the first
 * after ble_deinit), as with Arduino-ESP32's BLEDevice::init(). */
static int ensure_init(const char *name)
{
    if (s_inited) {
        return BL_OK;
    }
    if (!s_ev) {
        s_ev = (bl_ble_event_t *)calloc(EVENT_QUEUE_LEN, sizeof(bl_ble_event_t));
        if (!s_ev) {
            return BL_ERR_NO_MEM;
        }
    }
    if (!s_hci_rx) {
        s_hci_rx = (uint8_t *)malloc(HCI_RX_SIZE);
        if (!s_hci_rx) {
            return BL_ERR_NO_MEM;
        }
    }
    strlcpy(s_name, name && *name ? name : "Baseline", sizeof(s_name));
    if (!s_stack_up) {
        int ok = 0;
        on_worker([&ok] { ok = BLE.begin(); });
        if (!ok) {
            return BL_ERR_FAIL;
        }
        CordioTransport::set_data_received_handler(hci_received);
        s_hci_ticker.attach(hci_move, std::chrono::milliseconds(5));
        s_mtu_limit = ATT._maxMtu;
        s_stack_up = true;
    }
    BLE.setDeviceName(s_name);
    adv_defaults();
    s_inited = true;
    return BL_OK;
}

/* ---- advertising ---- */

/* Builds the advertisement and scan response from the settings. */
static void build_adv(void)
{
    auto put = [](uint8_t *buf, uint8_t &n, uint8_t type, const uint8_t *d, uint8_t k) -> bool {
        if (n + 2 + k > 31) {
            return false;
        }
        buf[n++] = (uint8_t)(k + 1);
        buf[n++] = type;
        memcpy(buf + n, d, k);
        n = (uint8_t)(n + k);
        return true;
    };
    uint8_t len = 0;
    const uint8_t flags = 0x06;     // LE general discoverable, no BR/EDR
    put(s_adv_buf, len, 0x01, &flags, 1);
    uint8_t list[16 * MAX_ADV_UUIDS];
    uint8_t k = 0;
    for (int i = 0; i < s_adv_uuid_count; i++) {
        if (s_adv_uuids[i].is16) {
            list[k++] = (uint8_t)s_adv_uuids[i].u16;
            list[k++] = (uint8_t)(s_adv_uuids[i].u16 >> 8);
        }
    }
    if (k) {
        put(s_adv_buf, len, 0x03, list, k);     // complete list of 16-bit UUIDs
    }
    k = 0;
    for (int i = 0; i < s_adv_uuid_count && !k; i++) {
        if (!s_adv_uuids[i].is16) {
            for (int j = 0; j < 16; j++) {
                list[j] = s_adv_uuids[i].u128[15 - j];
            }
            k = 16;
        }
    }
    if (k) {
        put(s_adv_buf, len, 0x07, list, 16);    // a 128-bit UUID (only one fits)
    }
    if (s_appearance) {
        uint8_t a[2] = { (uint8_t)s_appearance, (uint8_t)(s_appearance >> 8) };
        put(s_adv_buf, len, 0x19, a, 2);
    }
    if (s_manuf_len) {
        put(s_adv_buf, len, 0xFF, s_manuf, s_manuf_len);
    }
    if (s_pref_min || s_pref_max) {
        uint16_t lo = s_pref_min ? s_pref_min : 0xFFFF, hi = s_pref_max ? s_pref_max : 0xFFFF;     // 0xFFFF: no preference
        uint8_t r[4] = { (uint8_t)lo, (uint8_t)(lo >> 8), (uint8_t)hi, (uint8_t)(hi >> 8) };
        put(s_adv_buf, len, 0x12, r, 4);
    }
    uint8_t name_len = (uint8_t)strlen(s_adv_name);
    // The complete name goes in the advertisement when it fits, and always in
    // the scan response when that is on.
    if (name_len && !put(s_adv_buf, len, 0x09, (const uint8_t *)s_adv_name, name_len) && !s_scan_resp && len + 3 <= 31) {
        put(s_adv_buf, len, 0x08, (const uint8_t *)s_adv_name, (uint8_t)(31 - len - 2));
    }
    s_adv_len = len;
    s_sr_len = 0;
    if (s_scan_resp && name_len) {
        put(s_sr_buf, s_sr_len, 0x09, (const uint8_t *)s_adv_name, name_len <= 29 ? name_len : 29);
    }
}

static int advertise(void)
{
    build_adv();
    uint16_t lo = s_adv_min, hi = s_adv_max;
    if (lo < 0x20) lo = 0x20;           // 20 ms, the shortest the specification allows
    if (hi < lo) hi = lo;
    GAP.setAdvertisingInterval((uint16_t)((lo + hi) / 2));
    if (!GAP.advertise(s_adv_buf, s_adv_len, s_sr_buf, s_sr_len)) {
        return BL_ERR_FAIL;
    }
    s_advertising = true;
    return BL_OK;
}

/* A setting changed while advertising. While a central is connected the
 * controller isn't advertising; the library turns it back on when the central
 * leaves, with the data given here. */
static void readvertise(void)
{
    if (!s_advertising) {
        return;
    }
    if (central_count()) {
        build_adv();
        HCI.leSetAdvertisingData(s_adv_len, s_adv_buf);
        HCI.leSetScanResponseData(s_sr_len, s_sr_buf);
    } else {
        advertise();
    }
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

/* ---- server characteristics ---- */

static int chr_handle(BLELocalCharacteristic *c)
{
    for (int h = 0; h < MAX_CHARS; h++) {
        if (s_chars[h].obj == c) {
            return h;
        }
    }
    return -1;
}

static void on_written(BLEDevice, BLECharacteristic ch)
{
    BLELocalCharacteristic *c = ch._local;
    if (!c) {
        return;
    }
    if (c == s_nus_rx) {
        s_nus_in.put(c->_value, c->_valueLength);
        return;
    }
    push_data(BL_BLE_EV_WRITE, chr_handle(c), c->_value, c->_valueLength);
}

static void on_read(BLEDevice, BLECharacteristic ch)
{
    if (ch._local) {
        push_simple(BL_BLE_EV_READ, chr_handle(ch._local));    // sent just before the value is answered
    }
}

static BLELocalCharacteristic *new_char(const char *uuid_text, uint8_t props)
{
    BLELocalCharacteristic *c = new (std::nothrow) BLELocalCharacteristic(uuid_text, props, VALUE_MAX);
    if (!c) {
        return NULL;
    }
    if (!c->_value) {
        delete c;
        return NULL;
    }
    c->retain();                        // Baseline's reference (see bl_ble_reset)
    c->setEventHandler(BLEWritten, on_written);
    c->setEventHandler((BLECharacteristicEvent)BLERead, on_read);
    return c;
}

static void set_raw(BLELocalCharacteristic *c, const uint8_t *d, size_t n)
{
    if (n > VALUE_MAX) {
        n = VALUE_MAX;
    }
    if (n) {
        memcpy(c->_value, d, n);
    }
    c->_valueLength = (uint16_t)n;
}

/* ---- BLE serial (Nordic UART Service) ---- */

extern "C" int bl_impl_ble_serial_begin(const char *name)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    if (s_nus_tx) {
        return BL_ERR_STATE;
    }
    if (!s_nus_in.alloc(NUS_BUFFER) || !s_nus_out.alloc(NUS_BUFFER)) {
        return BL_ERR_NO_MEM;
    }
    int st = ensure_init(name);
    if (st != BL_OK) {
        return st;
    }
    BLELocalService *svc = new (std::nothrow) BLELocalService(NUS_SERVICE);
    BLELocalCharacteristic *tx = new_char(NUS_TX, BLENotify);
    BLELocalCharacteristic *rx = new_char(NUS_RX, BLEWrite | BLEWriteWithoutResponse);
    if (!svc || !tx || !rx) {
        delete svc;
        delete tx;
        delete rx;
        return BL_ERR_NO_MEM;
    }
    svc->retain();
    svc->addCharacteristic(tx);
    svc->addCharacteristic(rx);
    GATT.addService(svc);
    s_nus_svc = svc;
    s_nus_tx = tx;
    s_nus_rx = rx;
    s_nus_in.clear();
    s_nus_out.clear();
    Uuid su;
    parse_uuid(NUS_SERVICE, su);
    adv_add_uuid(su);
    s_scan_resp = true;
    return advertise();
}

extern "C" int bl_impl_ble_serial_connected(void)
{
    return central_count() > 0 ? 1 : 0;
}

/* BLE serial output waits in s_nus_out and goes out from bl_ble_poll() and
 * the writes, as many bytes per notification as the MTU takes, whenever the
 * controller has a free buffer: bytes written one at a time while it is busy
 * are sent together. */
static void nus_drain(void)
{
    if (!s_nus_tx || !s_nus_out.count) {
        return;
    }
    uint16_t mtu = 0xFFFF;
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        if (ATT._peers[i].connectionHandle != NO_CON && ATT._peers[i].role == ROLE_PERIPHERAL &&
            ATT._peers[i].mtu < mtu) {
            mtu = ATT._peers[i].mtu;
        }
    }
    if (mtu == 0xFFFF || !(s_nus_tx->_cccdValue & 1)) {
        s_nus_out.clear();              // nobody connected or subscribed: dropped, as on the ESP32
        return;
    }
    uint8_t chunk[VALUE_MAX];
    while (s_nus_out.count && HCI._pendingPkt < HCI._maxPkt) {
        size_t n = 0;
        while (n < (size_t)(mtu - 3) && s_nus_out.count) {
            chunk[n++] = (uint8_t)s_nus_out.get();
        }
        send_value(s_nus_tx, chunk, n, false);
    }
}

extern "C" int bl_impl_ble_serial_write(const uint8_t *data, size_t len)
{
    if (s_worker_busy) {
        return 0;        // a library call is running on the helper thread
    }
    if (!s_nus_tx || !data || central_count() <= 0) {
        return 0;
    }
    if (!(s_nus_tx->_cccdValue & 1)) {
        return (int)len;                // not subscribed: dropped
    }
    size_t done = 0;
    while (done < len) {
        done += s_nus_out.put(data + done, len - done);
        nus_drain();
        if (done < len && !wait_until([] { return s_nus_out.count < s_nus_out.size || central_count() <= 0; }, 1000)) {
            break;                      // the buffer stayed full for a second
        }
        if (central_count() <= 0) {
            break;
        }
    }
    return (int)done;
}

extern "C" int bl_impl_ble_serial_available(void)
{
    bl_poll();
    return s_nus_in.count;
}

extern "C" int bl_impl_ble_serial_read(void)
{
    bl_poll();
    return s_nus_in.buf ? s_nus_in.get() : -1;
}

/* ---- device ---- */

extern "C" int bl_impl_ble_init(const char *name)
{
    return ensure_init(name);
}

extern "C" int bl_impl_ble_scan_stop(void);

/* Stops everything and forgets the server's services, characteristics,
 * descriptors and the remote characteristics. The radio stays on. Also
 * called when the sketch restarts without a chip reset (deep sleep). */
void bl_ble_reset(void)
{
    if (!s_stack_up) {
        return;
    }
    bl_impl_ble_scan_stop();
    GAP.stopAdvertise();
    s_advertising = false;
    for (int i = 0; i < ATT_MAX_PEERS; i++) {
        if (ATT._peers[i].connectionHandle != NO_CON) {
            HCI.disconnect(ATT._peers[i].connectionHandle);
        }
    }
    wait_until([] { return !any_peer(); }, 2000);
    // The database goes, then the objects it pointed at.
    GATT.end();
    for (int h = 0; h < MAX_SERVICES; h++) {
        if (s_services[h].obj) {
            s_services[h].obj->clear();         // the characteristics are deleted below
            delete s_services[h].obj;
        }
        s_services[h] = Svc();
    }
    for (int h = 0; h < MAX_CHARS; h++) {
        delete s_chars[h].obj;                  // deletes its descriptors
        s_chars[h] = Chr();
    }
    for (int h = 0; h < MAX_DESCS; h++) {
        free(s_descs[h].value);
        s_descs[h] = Desc();
    }
    if (s_nus_svc) {
        s_nus_svc->clear();
        delete s_nus_svc;
        delete s_nus_tx;
        delete s_nus_rx;
        s_nus_svc = NULL;
        s_nus_tx = s_nus_rx = NULL;
    }
    s_nus_in.clear();
    s_nus_out.clear();
    GATT.begin();
    if (s_remote) {
        memset(s_remote, 0, sizeof(Remote) * MAX_REMOTE);
    }
    s_client_con = NO_CON;
    s_result_count = 0;
    s_ev_head = s_ev_count = 0;
    ATT.setMaxMtu(s_mtu_limit);
    s_name[0] = 0;
    adv_defaults();
    s_inited = false;
}

extern "C" void bl_impl_ble_deinit(void)
{
    bl_ble_reset();
}

/* From bl_poll(): the library's input, the scan's end. */
void bl_ble_poll(void)
{
    if (!s_stack_up || s_worker_busy) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        hci_move();
        HCI.poll();
        if (!s_hci_count) {
            break;
        }
    }
    nus_drain();
    if (s_scanning && s_scan_ms && millis() - s_scan_start >= s_scan_ms) {
        HCI.leSetScanEnable(false, false);
        s_scanning = false;
        push_simple(BL_BLE_EV_SCAN_DONE, -1);
    }
}

extern "C" int bl_impl_ble_address(char *buf, size_t size)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
    }
    uint8_t a[6] = {};
    HCI.readBdAddr(a);
    char text[18];
    addr_text(a, text);
    if (buf && size) {
        strlcpy(buf, text, size);
    }
    return (int)strlen(text);
}

extern "C" int bl_impl_ble_set_mtu(uint16_t mtu)
{
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
    }
    if (mtu < 23) mtu = 23;
    ATT.setMaxMtu(mtu < s_mtu_limit ? mtu : s_mtu_limit);     // offered in the next MTU exchange
    return BL_OK;
}

/* ---- GATT server ---- */

extern "C" int bl_impl_ble_server_create(void)
{
    int st = ensure_init(NULL);
    return st == BL_OK ? 0 : st;
}

extern "C" int bl_impl_ble_server_connected_count(void)
{
    return central_count();
}

extern "C" void bl_impl_ble_server_disconnect(uint16_t conn_id)
{
    if (s_worker_busy) {
        return;                     // a library call is running on the helper thread
    }
    if (s_stack_up && peer_role(conn_id) == ROLE_PERIPHERAL) {
        HCI.disconnect(conn_id);
    }
}

extern "C" int bl_impl_ble_service_create(const char *uuid, int num_handles)
{
    (void)num_handles;                  // ArduinoBLE's database has no per-service reservation
    Uuid u;
    if (!parse_uuid(uuid, u)) {
        return BL_ERR_ARG;
    }
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
    }
    for (int h = 0; h < MAX_SERVICES; h++) {
        Svc &s = s_services[h];
        if (!s.obj) {
            lib_text(u, s.uuid);
            s.obj = new (std::nothrow) BLELocalService(s.uuid);
            if (!s.obj) {
                return BL_ERR_NO_MEM;
            }
            s.obj->retain();            // Baseline's reference: GATT.end() deletes what it holds alone
            s.started = false;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

/* Adds the service to the attribute database: its characteristics and
 * descriptors are fixed from now on. */
extern "C" int bl_impl_ble_service_start(int svc)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc].obj) {
        return BL_ERR_ARG;
    }
    if (!s_services[svc].started) {
        GATT.addService(s_services[svc].obj);
        s_services[svc].started = true;
    }
    return BL_OK;
}

static uint8_t to_lib_props(uint32_t p)
{
    uint8_t r = 0;
    if (p & BL_BLE_PROP_READ) r |= BLERead;
    if (p & BL_BLE_PROP_WRITE) r |= BLEWrite;
    if (p & BL_BLE_PROP_NOTIFY) r |= BLENotify;
    if (p & BL_BLE_PROP_INDICATE) r |= BLEIndicate;
    if (p & BL_BLE_PROP_WRITE_NR) r |= BLEWriteWithoutResponse;
    if (p & BL_BLE_PROP_BROADCAST) r |= BLEBroadcast;
    return r;
}

extern "C" int bl_impl_ble_char_create(int svc, const char *uuid, uint32_t properties)
{
    Uuid u;
    if (svc < 0 || svc >= MAX_SERVICES || !s_services[svc].obj || !parse_uuid(uuid, u)) {
        return BL_ERR_ARG;
    }
    if (s_services[svc].started) {
        return BL_ERR_STATE;            // its handles are given out
    }
    for (int h = 0; h < MAX_CHARS; h++) {
        Chr &c = s_chars[h];
        if (!c.obj) {
            lib_text(u, c.uuid);
            c.obj = new_char(c.uuid, to_lib_props(properties));
            if (!c.obj) {
                return BL_ERR_NO_MEM;
            }
            c.svc = svc;
            s_services[svc].obj->addCharacteristic(c.obj);
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

static BLELocalCharacteristic *char_of(int h)
{
    return (h >= 0 && h < MAX_CHARS) ? s_chars[h].obj : NULL;
}

extern "C" int bl_impl_ble_char_set_value(int ch, const uint8_t *data, size_t len)
{
    BLELocalCharacteristic *c = char_of(ch);
    if (!c || (!data && len)) {
        return BL_ERR_ARG;
    }
    set_raw(c, data, len);              // up to 512 bytes; not sent (that's notify())
    return BL_OK;
}

extern "C" int bl_impl_ble_char_get_value(int ch, uint8_t *buf, size_t size)
{
    BLELocalCharacteristic *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    size_t len = c->_valueLength;
    if (buf && size && len) {
        memcpy(buf, c->_value, len < size ? len : size);
    }
    return (int)len;
}

/* Sends the value to the centrals if they subscribed (CCCD bit 1
 * notifications, bit 2 indications). */
extern "C" int bl_impl_ble_char_notify(int ch, int indicate)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    BLELocalCharacteristic *c = char_of(ch);
    if (!c) {
        return BL_ERR_ARG;
    }
    if (!(c->_cccdValue & (indicate ? 2 : 1))) {
        return BL_OK;                   // nobody subscribed
    }
    send_value(c, c->_value, c->_valueLength, indicate != 0);
    return BL_OK;
}

/* The CCCD (2902) is the library's own, added with notify or indicate. Any
 * other descriptor is read-only, with the value given here. */
extern "C" int bl_impl_ble_char_add_descriptor(int ch, const char *uuid, const uint8_t *value, size_t len)
{
    BLELocalCharacteristic *c = char_of(ch);
    Uuid u;
    if (!c || !parse_uuid(uuid, u) || (!value && len) || len > VALUE_MAX) {
        return BL_ERR_ARG;
    }
    if (u.is16 && u.u16 == 0x2902) {
        return BL_OK;
    }
    if (s_services[s_chars[ch].svc].started) {
        return BL_ERR_STATE;
    }
    for (int h = 0; h < MAX_DESCS; h++) {
        Desc &d = s_descs[h];
        if (!d.obj) {
            d.value = (uint8_t *)malloc(len ? len : 1);
            if (!d.value) {
                return BL_ERR_NO_MEM;
            }
            if (len) {
                memcpy(d.value, value, len);
            }
            lib_text(u, d.uuid);
            d.obj = new (std::nothrow) BLELocalDescriptor(d.uuid, d.value, (int)len);
            if (!d.obj) {
                free(d.value);
                d.value = NULL;
                return BL_ERR_NO_MEM;
            }
            d.obj->retain();            // the characteristic's reference: it deletes the descriptor
            c->_descriptors.add(d.obj);
            return BL_OK;
        }
    }
    return BL_ERR_NO_MEM;
}

/* ---- advertising ---- */

extern "C" int bl_impl_ble_adv_config(int what, const char *text, const uint8_t *data, size_t value)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
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
    case BL_BLE_ADV_APPEARANCE:
        s_appearance = (uint16_t)value;
        BLE.setAppearance(s_appearance);
        break;
    case BL_BLE_ADV_MIN_PREFERRED: s_pref_min = (uint16_t)value; break;
    case BL_BLE_ADV_MAX_PREFERRED: s_pref_max = (uint16_t)value; break;
    case BL_BLE_ADV_MIN_INTERVAL:  s_adv_min = (uint16_t)value; break;
    case BL_BLE_ADV_MAX_INTERVAL:  s_adv_max = (uint16_t)value; break;
    case BL_BLE_ADV_RESET:         adv_defaults(); break;
    default:                       return BL_ERR_ARG;
    }
    readvertise();
    return BL_OK;
}

extern "C" int bl_impl_ble_adv_start(void)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    int st = ensure_init(NULL);
    return st == BL_OK ? advertise() : st;
}

extern "C" int bl_impl_ble_adv_stop(void)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    if (!s_inited) {
        return BL_ERR_STATE;
    }
    GAP.stopAdvertise();                // connections and the GATT server stay up
    s_advertising = false;
    return BL_OK;
}

/* ---- scanning ---- */

static uint16_t scan_units(uint32_t ms)
{
    uint32_t u = ms * 8 / 5;            // 0.625 ms units
    return (uint16_t)(u < 4 ? 4 : u > 0x4000 ? 0x4000 : u);
}

extern "C" int bl_impl_ble_scan_start(uint32_t seconds, int active, uint16_t interval_ms, uint16_t window_ms)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
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
    s_result_count = 0;
    HCI.leSetScanEnable(false, false);
    if (HCI.leSetScanParameters(active ? 0x01 : 0x00, itv, win, 0x00, 0x00) != 0) {
        return BL_ERR_ARG;
    }
    // Every report, not the controller's duplicate filter: with it on, this
    // controller drops the scan responses (names). scan_report() merges them.
    if (HCI.leSetScanEnable(true, false) != 0) {
        return BL_ERR_FAIL;
    }
    s_scanning = true;
    s_scan_start = millis();
    s_scan_ms = (seconds ? seconds : 5) * 1000;
    return BL_OK;
}

extern "C" int bl_impl_ble_scan_stop(void)
{
    if (s_stack_up && s_scanning) {
        HCI.leSetScanEnable(false, false);
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
    return s_result_count;
}

extern "C" int bl_impl_ble_scan_result(int index, bl_ble_adv_t *dev)
{
    if (!dev || index < 0 || index >= s_result_count) {
        return BL_ERR_ARG;
    }
    *dev = s_results[index];
    return BL_OK;
}

extern "C" void bl_impl_ble_scan_clear(void)
{
    s_result_count = 0;
}

/* ---- client ----
 * One client connection (handle 0). Services and characteristics are found
 * when the sketch asks for one, as on the ESP32. */

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
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    uint8_t a[6];
    if (!address || !parse_addr(address, a)) {
        return BL_ERR_ARG;
    }
    int st = ensure_init(NULL);
    if (st != BL_OK) {
        return st;
    }
    if (s_client_con != NO_CON) {
        return BL_ERR_NO_MEM;           // one client connection
    }
    uint8_t type = (uint8_t)(addr_type >= 0 && addr_type <= 3 ? addr_type : 0);
    ATT.setTimeout(5000);
    bool ok = false;
    on_worker([&] { ok = ATT.connect(type, a); });
    if (!ok || s_client_con == NO_CON) {
        return BL_ERR_FAIL;
    }
    s_client_type = type;
    memcpy(s_client_addr, a, 6);
    return 0;
}

extern "C" void bl_impl_ble_client_disconnect(int cl)
{
    if (cl != 0 || s_client_con == NO_CON) {
        return;
    }
    HCI.disconnect(s_client_con);
    wait_until([] { return s_client_con == NO_CON; }, 2000);
}

extern "C" int bl_impl_ble_client_connected(int cl)
{
    return (cl == 0 && s_client_con != NO_CON) ? 1 : 0;
}

extern "C" int bl_impl_ble_client_rssi(int cl)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    if (cl != 0 || s_client_con == NO_CON) {
        return 0;
    }
    int r = HCI.readRssi(s_client_con);
    return r == 127 ? 0 : r;
}

/* Finds the service, then the characteristic in it, on the connected peripheral. */
static bool discover(Remote &r)
{
    char svc[37], chr[37];
    lib_text(r.svc, svc);
    lib_text(r.chr, chr);
    bool ok = false;
    on_worker([&] { ok = ATT.discoverAttributes(s_client_type, s_client_addr, svc); });
    if (!ok) {
        return false;
    }
    BLERemoteDevice *dev = ATT.device(s_client_type, s_client_addr);
    if (!dev) {
        return false;
    }
    for (unsigned i = 0; i < dev->serviceCount(); i++) {
        BLERemoteService *s = dev->service(i);
        if (strcasecmp(s->uuid(), svc) != 0) {
            continue;
        }
        for (unsigned j = 0; j < s->characteristicCount(); j++) {
            BLERemoteCharacteristic *c = s->characteristic(j);
            if (strcasecmp(c->uuid(), chr) != 0) {
                continue;
            }
            r.value_handle = c->valueHandle();
            r.props = c->properties();
            r.cccd_handle = (uint16_t)(r.value_handle + 1);     // where it usually is
            for (unsigned k = 0; k < c->descriptorCount(); k++) {
                if (strcmp(c->descriptor(k)->uuid(), "2902") == 0) {
                    r.cccd_handle = c->descriptor(k)->handle();
                }
            }
            r.con = s_client_con;
            return r.con != NO_CON;
        }
    }
    return false;
}

extern "C" int bl_impl_ble_remote_char(int cl, const char *service_uuid, const char *char_uuid)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
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
        memset(&r, 0, sizeof(r));
        r.svc = su;
        r.chr = cu;
        r.con = NO_CON;
    }
    if (!discover(r)) {
        return BL_ERR_FAIL;
    }
    r.used = true;
    r.listening = false;
    return h;
}

/* Reads one ATT packet of the value (MTU - 1 bytes). Returns the value's length. */
extern "C" int bl_impl_ble_remote_read(int rc, uint8_t *buf, size_t size)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    Remote *r = remote_of(rc);
    if (!r) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    static uint8_t resp[256];
    int n = 0;
    on_worker([&] { n = ATT.readReq(r->con, r->value_handle, resp); });
    if (n <= 0) {
        return remote_live(r) ? BL_ERR_TIMEOUT : BL_ERR_STATE;
    }
    if (resp[0] == 0x01) {
        return BL_ERR_FAIL;             // the peripheral answered with an error
    }
    n--;
    if (buf && size) {
        memcpy(buf, resp + 1, (size_t)n < size ? (size_t)n : size);
    }
    return n;
}

extern "C" int bl_impl_ble_remote_write(int rc, const uint8_t *data, size_t len, int response)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    Remote *r = remote_of(rc);
    if (!r || (!data && len)) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    if (len > (size_t)(ATT.mtu(r->con) - 3)) {
        return BL_ERR_ARG;              // one packet: no long writes here
    }
    if (!response) {
        if (!acl_room(1000)) {
            return BL_ERR_TIMEOUT;
        }
        ATT.writeCmd(r->con, r->value_handle, data, (uint8_t)len);
        return BL_OK;
    }
    uint8_t resp[8];
    int n = 0;
    on_worker([&] { n = ATT.writeReq(r->con, r->value_handle, data, (uint8_t)len, resp); });
    if (n <= 0) {
        return remote_live(r) ? BL_ERR_TIMEOUT : BL_ERR_STATE;
    }
    return resp[0] == 0x01 ? BL_ERR_FAIL : BL_OK;
}

extern "C" int bl_impl_ble_remote_subscribe(int rc, int mode)
{
    if (s_worker_busy) {
        return BL_ERR_STATE;        // a library call is running on the helper thread
    }
    Remote *r = remote_of(rc);
    if (!r || mode < 0 || mode > 2) {
        return BL_ERR_ARG;
    }
    if (!remote_live(r)) {
        return BL_ERR_STATE;
    }
    uint8_t cfg[2] = { (uint8_t)(mode == 1 ? 1 : mode == 2 ? 2 : 0), 0 };
    r->listening = mode != 0;           // first, so the first value after subscribing is not missed
    uint8_t resp[8];
    int n = 0;
    on_worker([&] { n = ATT.writeReq(r->con, r->cccd_handle, cfg, 2, resp); });
    bool ok = n > 0 && resp[0] != 0x01;
    if (!ok) {
        r->listening = false;
    }
    if (n <= 0) {
        return remote_live(r) ? BL_ERR_TIMEOUT : BL_ERR_STATE;
    }
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_ble_remote_props(int rc)
{
    Remote *r = remote_of(rc);
    if (!r) {
        return 0;
    }
    uint8_t p = r->props;
    uint32_t b = 0;
    if (p & BLERead) b |= BL_BLE_PROP_READ;
    if (p & BLEWrite) b |= BL_BLE_PROP_WRITE;
    if (p & BLENotify) b |= BL_BLE_PROP_NOTIFY;
    if (p & BLEIndicate) b |= BL_BLE_PROP_INDICATE;
    if (p & BLEWriteWithoutResponse) b |= BL_BLE_PROP_WRITE_NR;
    if (p & BLEBroadcast) b |= BL_BLE_PROP_BROADCAST;
    return b;
}

/* ---- events ---- */

extern "C" int bl_impl_ble_event_take(bl_ble_event_t *ev)
{
    if (!ev || !s_ev || !s_ev_count) {
        return 0;
    }
    *ev = s_ev[s_ev_head];
    s_ev_head = (uint8_t)((s_ev_head + 1) % EVENT_QUEUE_LEN);
    s_ev_count--;
    return 1;
}

#else /* !BL_HAS_BLE */

void bl_ble_reset(void)
{
}

void bl_ble_poll(void)
{
}

#endif /* BL_HAS_BLE */
