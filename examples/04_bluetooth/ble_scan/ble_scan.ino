// ble_scan.ino - list nearby BLE devices, the classic Arduino way.
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BLEScan *scan;

class FoundDevice : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override
    {
        Serial.printf("  %s  %4d dBm  %s\n", device.getAddress().toString().c_str(), device.getRSSI(),
                      device.haveName() ? device.getName().c_str() : "");
    }
};

void setup()
{
    BLEDevice::init("");
    scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new FoundDevice());
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
}

void loop()
{
    Serial.println("Scanning for 5 seconds...");
    BLEScanResults *found = scan->start(5, false);
    Serial.printf("Found %d device(s)\n\n", found->getCount());
    scan->clearResults();
    delay(2000);
}
