// 09_espnow_broadcast.ino - broadcast a message every 2 s over ESP-NOW and print
// whatever other boards send. Load it on two or more boards to see them talk.
// Standard ESP-NOW calls; Baseline does the radio work, and the callbacks run
// on the sketch's own task, so they can use Serial safely.
#include <WiFi.h>
#include <esp_now.h>

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
int count = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *mac = info->src_addr;
    Serial.printf("from %02X:%02X:%02X:%02X:%02X:%02X: %.*s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len, (const char *)data);
}

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "  sent" : "  send failed");
}

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onReceive);
    esp_now_register_send_cb(onSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddress, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    Serial.print("ESP-NOW ready, my MAC is ");
    Serial.println(WiFi.macAddress());
}

void loop()
{
    String msg = "hello #" + String(count++) + " from " + WiFi.macAddress();
    esp_now_send(broadcastAddress, (const uint8_t *)msg.c_str(), msg.length());
    delay(2000);
}
