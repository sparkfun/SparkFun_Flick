// net_test.ino - the networking and Bluetooth additions of ABI 1.10 (ESP32
// family). Needs WIFI_SSID / WIFI_PASSWORD (python flick.py wifi "network" "password").
// One PASS/FAIL line per check, then a RESULT line.
//
//   - WiFi.onEvent with details: cb(event, info) and cb(arduino_event_t *)
//   - Network.eventName / getDefaultInterface, WiFi.STA as a NetworkInterface
//   - esp_wifi_get_channel, WiFi.setLongRange, scanNetworks on one channel
//   - WiFiClient localIP/localPort, WiFiClientSecure lastError
//   - ESP-NOW peer list (esp_now_get_peer_num, esp_now_fetch_peer)
//   - BluetoothSerial receive/send buffer sizes (classic ESP32)
#if !defined(BL_PLATFORM_ESP32) || !defined(BL_HAS_WIFI)
#error "This test needs an ESP32 with WiFi."
#endif
#if !defined(WIFI_SSID)
#error "Set the WiFi network first: python flick.py wifi \"network\" \"password\""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#if defined(BL_HAS_BT_CLASSIC)
#include <BluetoothSerial.h>
BluetoothSerial SerialBT;
#endif

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

static volatile bool gotIp, gotConnected, sysSeen;
static uint32_t eventIp;
static uint8_t eventChannel, eventSsidLen;

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        eventIp = info.got_ip.ip_info.ip.addr;
        gotIp = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        eventChannel = info.wifi_sta_connected.channel;
        eventSsidLen = info.wifi_sta_connected.ssid_len;
        gotConnected = true;
    }
}

static void onSysEvent(arduino_event_t *event)
{
    if (event->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        sysSeen = true;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("net_test: start");

    Network.onEvent(onNetEvent);
    WiFi.onEvent(onSysEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.mode(WIFI_STA);
    CHECK("WiFi.STA.connect starts", WiFi.STA.connect(WIFI_SSID, WIFI_PASSWORD));
    for (uint32_t start = millis(); !gotIp && millis() - start < 20000;) {
        delay(50);
    }
    CHECK("event cb(event, info): GOT_IP", gotIp);
    CHECK("event cb(arduino_event_t *): GOT_IP", sysSeen);
    CHECK("event details: got_ip address is localIP", gotIp && IPAddress(eventIp) == WiFi.localIP());
    CHECK("event details: connected SSID and channel", gotConnected && eventSsidLen == strlen(WIFI_SSID) &&
                                                           eventChannel == WiFi.channel());
    const char *name = Network.eventName(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    Serial.printf("      eventName(GOT_IP) = %s\n", name);
    CHECK("Network.eventName", name && strcmp(name, "UNKNOWN") != 0 && strlen(name) > 0);

    CHECK("WiFi.STA started, connected, hasIP, linkUp",
          WiFi.STA.started() && WiFi.STA.connected() && WiFi.STA.hasIP() && WiFi.STA.linkUp());
    CHECK("WiFi.STA.localIP", WiFi.STA.localIP() == WiFi.localIP());
    CHECK("WiFi.STA gateway and subnet", WiFi.STA.gatewayIP() == WiFi.gatewayIP() && WiFi.STA.subnetMask() == WiFi.subnetMask());
    CHECK("WiFi.STA.macAddress", WiFi.STA.macAddress() == WiFi.macAddress());
    CHECK("WiFi.STA.SSID", WiFi.STA.SSID() == WIFI_SSID);
    CHECK("WiFi.STA.RSSI", WiFi.STA.RSSI() < 0);
    CHECK("WiFi.STA.status", WiFi.STA.status() == WL_CONNECTED);
    CHECK("WiFi.STA is the default", WiFi.STA.isDefault() && Network.getDefaultInterface() == &WiFi.STA);
    Serial.print("      ");
    size_t printed = Serial.print(WiFi.STA);
    Serial.println();
    CHECK("NetworkInterface prints", printed > 10);
    CHECK("WiFi.AP not started", !WiFi.AP.started());

    WiFiClient client;
    client.setConnectionTimeout(5000);
    if (client.connect("example.com", 80)) {
        CHECK("WiFiClient.localIP", client.localIP() == WiFi.localIP());
        CHECK("WiFiClient.localPort", client.localPort() != 0);
        client.stop();
    } else {
        CHECK("WiFiClient connect example.com:80", false);
    }

    // A good handshake first, so the failure below is the certificate's
    Serial.printf("      internal heap: %u free, %u largest block\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    WiFiClientSecure good;
    good.setHandshakeTimeout(10);
    CHECK("WiFiClientSecure connects with the CA bundle", good.connect("example.com", 443));
    good.stop();
    WiFiClientSecure tls;
    static const char wrongCa[] = "-----BEGIN CERTIFICATE-----\nnot a certificate\n-----END CERTIFICATE-----\n";
    tls.setCACert(wrongCa);
    bool connected = tls.connect("example.com", 443);
    char err[100];
    int code = tls.lastError(err, sizeof(err));
    Serial.printf("      lastError %d: %s\n", code, err);
    CHECK("WiFiClientSecure refuses a bad CA and reports lastError", !connected && code != 0 && err[0]);

    uint8_t primary = 0;
    wifi_second_chan_t second;
    CHECK("esp_wifi_get_channel", esp_wifi_get_channel(&primary, &second) == ESP_OK && primary == WiFi.channel());
    int found = WiFi.scanNetworks(false, false, false, 300, primary);
    Serial.printf("      %d networks on channel %d\n", found, primary);
    CHECK("scanNetworks on one channel finds ours", found >= 1);
    WiFi.scanDelete();

    // Changing the station's protocols makes it reconnect
    CHECK("WiFi.setLongRange(true)", WiFi.setLongRange(true));
    CHECK("WiFi.setLongRange(false)", WiFi.setLongRange(false, WIFI_IF_STA));
    bool online = false;
    for (int i = 0; i < 30 && !online; i++) {
        WiFiClient again;
        again.setConnectionTimeout(2000);
        online = again.connect("example.com", 80);
        again.stop();
        Serial.printf("      %d s: status %d, hasIP %d, RSSI %d, connect %d\n", i, WiFi.STA.status(), WiFi.STA.hasIP(),
                      WiFi.STA.RSSI(), online);
        if (!online) {
            delay(1000);
        }
    }
    CHECK("station back online after setLongRange", online);

    CHECK("esp_now_init", esp_now_init() == ESP_OK);
    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    CHECK("esp_now_add_peer (broadcast)", esp_now_add_peer(&peer) == ESP_OK);
    esp_now_peer_num_t count;
    CHECK("esp_now_get_peer_num", esp_now_get_peer_num(&count) == ESP_OK && count.total_num == 1);
    esp_now_peer_info_t fetched;
    CHECK("esp_now_fetch_peer", esp_now_fetch_peer(true, &fetched) == ESP_OK && fetched.peer_addr[0] == 0xFF &&
                                    esp_now_fetch_peer(false, &fetched) == ESP_ERR_ESPNOW_NOT_FOUND);
    uint8_t hello[] = "hello";
    CHECK("esp_now_send broadcast", esp_now_send(peer.peer_addr, hello, sizeof(hello)) == ESP_OK);

#if defined(BL_HAS_BT_CLASSIC)
    // Last: Bluetooth Classic takes much of the internal RAM that TLS needs
    SerialBT.setRxBufferSize(4096);
    SerialBT.setTxBufferSize(2048);
    CHECK("BluetoothSerial begin with 4 KB / 2 KB buffers", SerialBT.begin("flick-net-test"));
    static uint8_t block[1000];
    memset(block, 'x', sizeof(block));
    uint32_t t0 = millis();
    size_t n = SerialBT.write(block, sizeof(block));
    CHECK("BluetoothSerial write goes to the send buffer at once", n == sizeof(block) && millis() - t0 < 50);
    CHECK("BluetoothSerial nothing received", SerialBT.available() == 0);
    Serial.printf("      internal heap with Bluetooth Classic: %u free, %u largest block\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif

    Serial.printf("RESULT %d passed, %d failed\n", passed, failed);
}

void loop()
{
    delay(1000);
}
