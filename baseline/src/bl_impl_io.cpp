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

/* ---- I2C ---- */

extern "C" int bl_impl_i2c_begin(int sda, int scl, uint32_t freq)
{
    if (sda < 0 || scl < 0) {
        sda = BL_BOARD_SDA;     // the target's bl_board.h: Qwiic on SparkFun boards
        scl = BL_BOARD_SCL;
    } else if (!bl_pin_usable(sda) || !bl_pin_usable(scl)) {
        return BL_ERR_ARG;
    }
    return Wire.begin(sda, scl, freq ? freq : 100000) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_i2c_write(uint8_t addr, const uint8_t *data, size_t len, bool stop)
{
    if (len > I2C_BUFFER_LENGTH || (len && !data)) {
        return BL_ERR_ARG;
    }
    Wire.beginTransmission(addr);
    if (len) {
        Wire.write(data, len);
    }
    switch (Wire.endTransmission(stop)) {
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

extern "C" int bl_impl_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!data || len > I2C_BUFFER_LENGTH) {
        return BL_ERR_ARG;
    }
    size_t n = Wire.requestFrom(addr, len);
    for (size_t i = 0; i < n; i++) {
        data[i] = Wire.read();
    }
    return (int)n;
}
