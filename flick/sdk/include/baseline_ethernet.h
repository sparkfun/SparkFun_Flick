/*
 * baseline_ethernet.h - Ethernet with NativeEthernet's names (Teensy 4.1).
 *
 *   #include <NativeEthernet.h>
 *   uint8_t mac[6];
 *   void setup() {
 *       Ethernet.MACAddress(mac);              // the board's own address
 *       if (!Ethernet.begin(mac)) { ... }      // DHCP
 *       EthernetClient client;  client.connect("example.com", 80);
 *   }
 *
 * Baseline runs the network (NativeEthernet, FNET). EthernetClient, EthernetServer
 * and EthernetUDP are the SDK's network classes (WiFiClient, WiFiServer, WiFiUDP),
 * and HTTPClient works too, for plain http:// only (no TLS over Ethernet).
 * The web server and MQTT are WiFi-only for now.
 */
#ifndef BASELINE_ETHERNET_H
#define BASELINE_ETHERNET_H

#ifdef __cplusplus

enum EthernetLinkStatus { Unknown, LinkON, LinkOFF };
enum EthernetHardwareStatus { EthernetNoHardware, EthernetW5100, EthernetW5200, EthernetW5500 };

class EthernetClass {
public:
    constexpr EthernetClass() {}
    /* DHCP. Returns 1 with an address, 0 if none came within timeout. */
    int begin(uint8_t *mac, unsigned long timeout = 60000, unsigned long responseTimeout = 4000)
    {
        (void)responseTimeout;
        return bl_eth_begin(mac, 0, 0, 0, 0, (uint32_t)timeout);
    }
    void begin(uint8_t *mac, IPAddress ip) { bl_eth_begin(mac, ip, 0, 0, 0, 0); }
    void begin(uint8_t *mac, IPAddress ip, IPAddress dns) { bl_eth_begin(mac, ip, dns, 0, 0, 0); }
    void begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway) { bl_eth_begin(mac, ip, dns, gateway, 0, 0); }
    void begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet)
    {
        bl_eth_begin(mac, ip, dns, gateway, subnet, 0);
    }
    int maintain() { return bl_eth_maintain(); }
    EthernetLinkStatus linkStatus()
    {
        int s = bl_eth_status(BL_ETH_LINK);
        return s == 1 ? LinkON : s == 0 ? LinkOFF : Unknown;
    }
    /* The Teensy 4.1's PHY counts as present: there is no W5x00 chip. */
    EthernetHardwareStatus hardwareStatus() { return bl_eth_status(BL_ETH_HARDWARE) == 1 ? EthernetW5500 : EthernetNoHardware; }
    void MACAddress(uint8_t *mac) { bl_eth_mac(mac); }
    IPAddress localIP() { return IPAddress(bl_eth_ip(BL_IP_STA)); }
    IPAddress subnetMask() { return IPAddress(bl_eth_ip(BL_IP_SUBNET)); }
    IPAddress gatewayIP() { return IPAddress(bl_eth_ip(BL_IP_GATEWAY)); }
    IPAddress dnsServerIP() { return IPAddress(bl_eth_ip(BL_IP_DNS)); }
};
inline EthernetClass Ethernet;

typedef WiFiClient EthernetClient;
typedef WiFiServer EthernetServer;
typedef WiFiUDP EthernetUDP;

#endif /* __cplusplus */
#endif /* BASELINE_ETHERNET_H */
