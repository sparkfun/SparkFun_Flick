/* API table: SPI, on Arduino's SPI library (default pins from the board variant). */
#include <Arduino.h>
#include <SPI.h>
#include "gen/bl_impl.h"
#include "bl_pins.h"

static bool s_started;

static bool pin_ok(int pin)
{
    return pin < 0 || bl_pin_usable(pin);
}

extern "C" int bl_impl_spi_begin(int sck, int miso, int mosi, int ss)
{
    if (!pin_ok(sck) || !pin_ok(miso) || !pin_ok(mosi) || !pin_ok(ss)) {
        return BL_ERR_ARG;
    }
    SPI.begin(sck < 0 ? SCK : sck, miso < 0 ? MISO : miso, mosi < 0 ? MOSI : mosi, ss < 0 ? SS : ss);
    s_started = true;
    return BL_OK;
}

extern "C" void bl_impl_spi_end(void)
{
    if (s_started) {
        SPI.end();
        s_started = false;
    }
}

extern "C" void bl_impl_spi_begin_transaction(uint32_t clock, int bit_order, int data_mode)
{
    if (!s_started) {
        bl_impl_spi_begin(-1, -1, -1, -1);
    }
    SPI.beginTransaction(SPISettings(clock, bit_order == BL_LSBFIRST ? SPI_LSBFIRST : SPI_MSBFIRST,
                                     (uint8_t)(data_mode & 3)));
}

extern "C" void bl_impl_spi_end_transaction(void)
{
    SPI.endTransaction();
}

extern "C" void bl_impl_spi_transfer(uint8_t *data, size_t len)
{
    if (data && len) {
        SPI.transfer(data, len);
    }
}

extern "C" void bl_impl_spi_write(const uint8_t *data, size_t len)
{
    if (data && len) {
        SPI.writeBytes(data, len);
    }
}
