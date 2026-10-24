// 02_fuel_gauge.ino - read the LiPo battery with the board's MAX17048 fuel gauge.
// Plug a single-cell LiPo into the board's battery connector.
#include <Wire.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

SFE_MAX1704X lipo(MAX1704X_MAX17048);

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    if (!lipo.begin()) {
        Serial.println("MAX17048 not found on I2C");
        return;
    }
    lipo.quickStart();          // fresh estimate
    lipo.setThreshold(20);      // alert below 20 %
}

void loop()
{
    Serial.printf("Battery: %.3f V, %.1f %%, %+.1f %%/hour%s\n", lipo.getVoltage(), lipo.getSOC(),
                  lipo.getChangeRate(), lipo.getAlert() ? "  LOW BATTERY" : "");
    delay(2000);
}
