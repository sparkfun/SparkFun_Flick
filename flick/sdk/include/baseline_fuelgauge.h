/*
 * baseline_fuelgauge.h - LiPo fuel gauge (MAX17043/MAX17048) over Wire, with
 * the names of SparkFun's MAX1704x library. Both SparkFun Thing Plus boards
 * have a MAX17048 on the Qwiic I2C bus.
 *
 *   SFE_MAX1704X lipo(MAX1704X_MAX17048);
 *   Wire.begin();  lipo.begin();
 *   lipo.getVoltage();  lipo.getSOC();  lipo.getChangeRate();
 *
 * This is plain sketch code on top of Wire: no Baseline support needed.
 */
#ifndef BASELINE_FUELGAUGE_H
#define BASELINE_FUELGAUGE_H

#ifdef __cplusplus

typedef enum {
    MAX1704X_MAX17043 = 0,
    MAX1704X_MAX17044,
    MAX1704X_MAX17048,
    MAX1704X_MAX17049,
} sfe_max1704x_devices_e;

#define MAX1704x_ADDRESS 0x36

class SFE_MAX1704X {
public:
    SFE_MAX1704X(sfe_max1704x_devices_e device = MAX1704X_MAX17048) : device_(device) {}

    bool begin(TwoWire &wirePort = Wire) { wire_ = &wirePort; return isConnected(); }
    bool isConnected()
    {
        wire_->beginTransmission(MAX1704x_ADDRESS);
        return wire_->endTransmission() == 0;
    }
    void enableDebugging(Stream & = Serial) {}
    void disableDebugging() {}

    /* Battery voltage in volts. */
    float getVoltage()
    {
        uint16_t v = read16(REG_VCELL);
        if (is48()) {
            return v * 78.125e-6f * (device_ == MAX1704X_MAX17049 ? 2 : 1);
        }
        return (v >> 4) * 1.25e-3f * (device_ == MAX1704X_MAX17044 ? 2 : 1);
    }
    /* State of charge in percent. */
    float getSOC()
    {
        uint16_t s = read16(REG_SOC);
        return (s >> 8) + (s & 0xFF) / 256.0f;
    }
    /* Charge or discharge rate in percent per hour (MAX17048/9). */
    float getChangeRate() { return is48() ? (int16_t)read16(REG_CRATE) * 0.208f : 0.0f; }
    uint16_t getVersion() { return read16(REG_VERSION); }
    uint8_t getID() { return is48() ? (uint8_t)(read16(REG_VRESET_ID) & 0xFF) : 0; }

    uint8_t quickStart() { return write16(REG_MODE, 0x4000); }
    uint8_t reset() { return write16(REG_COMMAND, 0x5400); }

    /* Low-battery alert at this percent (1-32). */
    uint8_t setThreshold(uint8_t percent = 4)
    {
        percent = percent < 1 ? 1 : percent > 32 ? 32 : percent;
        uint16_t c = read16(REG_CONFIG);
        return write16(REG_CONFIG, (c & 0xFFE0) | (32 - percent));
    }
    uint8_t getThreshold() { return 32 - (read16(REG_CONFIG) & 0x1F); }
    bool getAlert(bool clear = false)
    {
        bool alert = (read16(REG_CONFIG) & 0x0020) != 0;
        if (alert && clear) {
            clearAlert();
        }
        return alert;
    }
    uint8_t clearAlert() { return write16(REG_CONFIG, read16(REG_CONFIG) & ~0x0020); }
    uint8_t enableSOCAlert() { return is48() ? write16(REG_CONFIG, read16(REG_CONFIG) | 0x0040) : 1; }
    uint8_t disableSOCAlert() { return is48() ? write16(REG_CONFIG, read16(REG_CONFIG) & ~0x0040) : 1; }

    /* Voltage alerts in volts (20 mV steps, MAX17048/9). */
    uint8_t setVALRTMax(float volts) { return setValrt(false, volts); }
    uint8_t setVALRTMin(float volts) { return setValrt(true, volts); }
    uint8_t getStatus() { return is48() ? (uint8_t)((read16(REG_STATUS) >> 8) & 0x7F) : 0; }
    bool isReset() { return getStatus() & 0x01; }
    bool isVoltageHigh() { return getStatus() & 0x02; }
    bool isVoltageLow() { return getStatus() & 0x04; }
    bool isVoltageReset() { return getStatus() & 0x08; }
    bool isLow() { return getStatus() & 0x10; }
    bool isChange() { return getStatus() & 0x20; }

    /* Hibernate thresholds (HIBRT register): the gauge hibernates when the
     * charge rate stays below HibThr (0.208 %/h steps) and wakes when the
     * voltage moves more than ActThr (1.25 mV steps). 0xFF in both: never hibernate. */
    uint8_t getHIBRTActThr() { return (uint8_t)(read16(REG_HIBRT) & 0xFF); }
    uint8_t setHIBRTActThr(uint8_t threshold) { return write16(REG_HIBRT, (read16(REG_HIBRT) & 0xFF00) | threshold); }
    uint8_t getHIBRTHibThr() { return (uint8_t)(read16(REG_HIBRT) >> 8); }
    uint8_t setHIBRTHibThr(uint8_t threshold) { return write16(REG_HIBRT, (read16(REG_HIBRT) & 0x00FF) | (threshold << 8)); }

    uint8_t sleep()
    {
        if (is48()) {
            write16(REG_MODE, read16(REG_MODE) | 0x2000);   // EnSleep
        }
        return write16(REG_CONFIG, read16(REG_CONFIG) | 0x0080);
    }
    uint8_t wake() { return write16(REG_CONFIG, read16(REG_CONFIG) & ~0x0080); }

private:
    enum {
        REG_VCELL = 0x02, REG_SOC = 0x04, REG_MODE = 0x06, REG_VERSION = 0x08, REG_HIBRT = 0x0A, REG_CONFIG = 0x0C,
        REG_VALRT = 0x14, REG_CRATE = 0x16, REG_VRESET_ID = 0x18, REG_STATUS = 0x1A, REG_COMMAND = 0xFE,
    };
    bool is48() const { return device_ == MAX1704X_MAX17048 || device_ == MAX1704X_MAX17049; }
    uint16_t read16(uint8_t reg)
    {
        wire_->beginTransmission(MAX1704x_ADDRESS);
        wire_->write(reg);
        if (wire_->endTransmission(false) != 0 || wire_->requestFrom(MAX1704x_ADDRESS, 2) != 2) {
            return 0;
        }
        uint8_t hi = (uint8_t)wire_->read();
        uint8_t lo = (uint8_t)wire_->read();
        return (uint16_t)(hi << 8 | lo);
    }
    uint8_t write16(uint8_t reg, uint16_t value)
    {
        wire_->beginTransmission(MAX1704x_ADDRESS);
        wire_->write(reg);
        wire_->write((uint8_t)(value >> 8));
        wire_->write((uint8_t)(value & 0xFF));
        return wire_->endTransmission();    // 0 = success, as in SparkFun's library
    }
    uint8_t setValrt(bool min, float volts)
    {
        if (!is48()) {
            return 1;
        }
        int steps = (int)(volts / 0.02f);
        uint8_t v = (uint8_t)(steps < 0 ? 0 : steps > 255 ? 255 : steps);
        uint16_t r = read16(REG_VALRT);
        r = min ? (uint16_t)((r & 0xFF00) | v) : (uint16_t)((r & 0x00FF) | (v << 8));
        return write16(REG_VALRT, r);
    }

    TwoWire *wire_ = &Wire;
    sfe_max1704x_devices_e device_;
};

#endif /* __cplusplus */
#endif /* BASELINE_FUELGAUGE_H */
