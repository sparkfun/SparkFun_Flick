/* API table: ADC, PWM, RGB LED, I2C - on Arduino's peripheral layer. */
#include <Arduino.h>
#include <Wire.h>
#include "gen/bl_impl.h"
#include "bl_port.h"

/* ---- analog / PWM / RGB ---- */

extern "C" int bl_impl_adc_read(int pin)
{
    return bl_pin_is_adc(pin) ? analogRead(pin) : BL_ERR_ARG;
}

extern "C" int bl_impl_adc_read_mv(int pin)
{
    return bl_pin_is_adc(pin) ? (int)analogReadMilliVolts(pin) : BL_ERR_ARG;
}

extern "C" int bl_impl_pwm_write(int pin, int value)
{
    if (!bl_pin_usable(pin) || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return BL_ERR_ARG;
    }
    analogWrite(pin, constrain(value, 0, 255));
    return BL_OK;
}

extern "C" int bl_impl_rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!bl_pin_usable(pin) || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return BL_ERR_ARG;
    }
#ifdef BL_PORT_RGB_WRITE
    bl_port_rgb_write(pin, red, green, blue);
#else
    rgbLedWrite(pin, red, green, blue);
#endif
    return BL_OK;
}

/* ---- I2C: bus 0 = Wire, bus 1 = Wire1 (the second controller; on the C6/C5
 * the low-power one). The original i2c_* calls are bus 0. ---- */

static TwoWire *bus_of(int bus)
{
    if (bus == 0) {
        return &Wire;
    }
#if SOC_I2C_NUM > 1
    if (bus == 1) {
        return &Wire1;
    }
#endif
    return NULL;
}

extern "C" int bl_impl_i2cx_begin(int bus, int sda, int scl, uint32_t freq)
{
    TwoWire *w = bus_of(bus);
    if (!w) {
        return bus == 1 ? BL_ERR_UNSUPPORTED : BL_ERR_ARG;
    }
    if (sda < 0 || scl < 0) {
        if (bus != 0) {
            return BL_ERR_ARG;      // Wire1 has no board default pins: pass them
        }
        sda = BL_BOARD_SDA;         // the target's bl_board.h: Qwiic on SparkFun boards
        scl = BL_BOARD_SCL;
    } else if (!bl_pin_usable(sda) || !bl_pin_usable(scl)) {
        return BL_ERR_ARG;
    }
    w->end();                       // begin() again with new pins or speed
    return w->begin(sda, scl, freq ? freq : 100000) ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_i2cx_end(int bus)
{
    TwoWire *w = bus_of(bus);
    if (w) {
        w->end();
    }
}

extern "C" int bl_impl_i2cx_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    TwoWire *w = bus_of(bus);
    if (!w || len > I2C_BUFFER_LENGTH || (len && !data)) {
        return BL_ERR_ARG;
    }
    w->beginTransmission(addr);
    if (len) {
        w->write(data, len);
    }
    switch (w->endTransmission(stop)) {
    case 0:
        return BL_OK;
    case 2:     // address NACK
    case 3:     // data NACK
        return BL_ERR_NACK;
    case 5:
        return BL_ERR_TIMEOUT;
    default:
        return BL_ERR_FAIL;
    }
}

extern "C" int bl_impl_i2cx_read(int bus, uint8_t addr, uint8_t *data, size_t len)
{
    TwoWire *w = bus_of(bus);
    if (!w || !data || len > I2C_BUFFER_LENGTH) {
        return BL_ERR_ARG;
    }
    size_t n = w->requestFrom(addr, len);
    for (size_t i = 0; i < n; i++) {
        data[i] = w->read();
    }
    return (int)n;
}

extern "C" int bl_impl_i2c_begin(int sda, int scl, uint32_t freq)
{
    return bl_impl_i2cx_begin(0, sda, scl, freq);
}

extern "C" int bl_impl_i2c_write(uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    return bl_impl_i2cx_write(0, addr, data, len, stop);
}

extern "C" int bl_impl_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    return bl_impl_i2cx_read(0, addr, data, len);
}
