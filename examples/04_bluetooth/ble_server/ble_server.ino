// ble_server.ino - a classic Arduino BLE server: one characteristic that counts up
// and notifies every second, and accepts writes from the phone.
// Try it with nRF Connect: connect to "Baseline-Server", enable notifications.
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *counterChar;
bool deviceConnected = false;
uint32_t counter = 0;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override
    {
        deviceConnected = true;
        Serial.println("Central connected");
    }
    void onDisconnect(BLEServer *server) override
    {
        deviceConnected = false;
        Serial.println("Central disconnected, advertising again");
        server->startAdvertising();
    }
};

class WriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        Serial.print("Phone wrote: ");
        Serial.println(c->getValue());
    }
};

void setup()
{
    BLEDevice::init("Baseline-Server");
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);
    counterChar = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    counterChar->addDescriptor(new BLE2902());
    counterChar->setCallbacks(new WriteCallbacks());
    counterChar->setValue("hello");
    service->start();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.print("Advertising as Baseline-Server, address ");
    Serial.println(BLEDevice::getAddress().toString());
}

void loop()
{
    if (deviceConnected) {
        counter++;
        counterChar->setValue(counter);
        counterChar->notify();
    }
    delay(1000);
}
