// 07_ethernet_http.ino - Ethernet on a Teensy 4.1: get an address, then fetch a web page.
// Solder or plug in the Teensy 4.1 Ethernet kit (MagJack) and connect a cable to a
// network with DHCP. Standard NativeEthernet code; HTTPClient works over it too, for
// plain http:// (there is no TLS over Ethernet).
#if !defined(BL_HAS_ETHERNET)
#error "This board has no Ethernet in its Baseline (the Teensy 4.1 has it)."
#endif
#include <NativeEthernet.h>
#include <HTTPClient.h>

uint8_t mac[6];

void setup()
{
    Serial.begin(115200);
    delay(1500);
    Ethernet.MACAddress(mac);                       // the Teensy's own address
    Serial.printf("MAC %02X:%02X:%02X:%02X:%02X:%02X, asking for an address...\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (!Ethernet.begin(mac, 15000)) {
        Serial.println(Ethernet.linkStatus() == LinkOFF ? "No cable connected" : "No DHCP answer");
        while (1);
    }
    Serial.print("IP ");
    Serial.print(Ethernet.localIP());
    Serial.print(", gateway ");
    Serial.print(Ethernet.gatewayIP());
    Serial.print(", DNS ");
    Serial.println(Ethernet.dnsServerIP());

    HTTPClient http;
    http.begin("http://example.com/");
    int status = http.GET();
    String page = http.getString();
    Serial.printf("GET http://example.com/ -> %d, %u bytes\n", status, page.length());
    int title = page.indexOf("<title>");
    if (title >= 0) {
        Serial.println(page.substring(title, page.indexOf("</title>") + 8));
    }
    http.end();

    // The same with a raw TCP connection, as with WiFiClient.
    EthernetClient client;
    uint32_t start = millis();
    if (client.connect("example.com", 80)) {
        client.print("HEAD / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
        while (client.connected() && !client.available()) {
            delay(1);
        }
        Serial.printf("TCP: connected and answered in %lu ms: ", (unsigned long)(millis() - start));
        Serial.println(client.readStringUntil('\n'));
        client.stop();
    }
}

void loop()
{
    Ethernet.maintain();        // renews the DHCP lease when due
    delay(1000);
}
