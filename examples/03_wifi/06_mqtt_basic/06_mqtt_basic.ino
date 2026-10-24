// 06_mqtt_basic.ino - publish and subscribe with PubSubClient.
// Uses the public test broker test.mosquitto.org. Set your WiFi below, then
// watch the messages with any MQTT client subscribed to "baseline/demo/#".
#include <WiFi.h>
#include <PubSubClient.h>

#ifndef WIFI_SSID      // secrets.cfg (see secrets.cfg.example) defines these
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#endif
const char *BROKER = "test.mosquitto.org";

WiFiClient net;
PubSubClient mqtt(net);
uint32_t lastPublish;

void onMessage(char *topic, byte *payload, unsigned int length)
{
    Serial.printf("message on %s: %.*s\n", topic, (int)length, (const char *)payload);
}

void reconnect()
{
    while (!mqtt.connected()) {
        Serial.printf("Connecting to %s... ", BROKER);
        String id = "baseline-" + String(random(0xffff), HEX);
        if (mqtt.connect(id.c_str())) {
            Serial.println("connected");
            mqtt.subscribe("baseline/demo/in");
        } else {
            Serial.printf("failed, state %d; retrying in 5 s\n", mqtt.state());
            delay(5000);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\nWiFi connected: ");
    Serial.println(WiFi.localIP());
    mqtt.setServer(BROKER, 1883);
    mqtt.setCallback(onMessage);
}

void loop()
{
    if (!mqtt.connected()) {
        reconnect();
    }
    mqtt.loop();
    if (millis() - lastPublish > 5000) {
        lastPublish = millis();
        String msg = "uptime " + String(millis() / 1000) + " s";
        mqtt.publish("baseline/demo/out", msg.c_str());
        Serial.println("published: " + msg);
    }
}
