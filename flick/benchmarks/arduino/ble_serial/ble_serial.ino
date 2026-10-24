// Plain Arduino-ESP32 equivalent of examples/04_bluetooth/01_ble_serial, for build-time comparison.
#include <BLEDevice.h>
#include <BLEServer.h>

#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *txChar;
volatile bool connected = false;
bool wasConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override { connected = true; }
    void onDisconnect(BLEServer *server) override
    {
        connected = false;
        server->startAdvertising();
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        Serial.write(c->getData(), c->getLength());
    }
};

void bleWrite(const uint8_t *data, size_t len)
{
    if (connected) {
        txChar->setValue(data, len);
        txChar->notify();
    }
}

void setup()
{
    Serial.begin(115200);
    BLEDevice::init("Baseline-UART");
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    BLEService *svc = server->createService(NUS_SERVICE);
    txChar = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic *rx = svc->createCharacteristic(
        NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    svc->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("Advertising as 'Baseline-UART'");
}

void loop()
{
    if (connected != wasConnected) {
        wasConnected = connected;
        Serial.println(connected ? "BLE connected" : "BLE disconnected");
        if (connected) {
            const char *hello = "Hello from Arduino!\r\n";
            bleWrite((const uint8_t *)hello, strlen(hello));
        }
    }
    while (Serial.available()) {
        uint8_t c = Serial.read();
        bleWrite(&c, 1);
    }
    delay(10);
}
