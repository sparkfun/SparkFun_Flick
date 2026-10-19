/* API table: hardware UARTs 1 and 2 (sketch Serial1 / Serial2). UART0 stays
 * with the ESP-IDF console; sketch Serial is the native USB port. */
#include <Arduino.h>
#include "gen/bl_impl.h"
#include "bl_pins.h"

#define SERIAL1_RX 44   // the pins labeled RX/TX on the Thing Plus
#define SERIAL1_TX 43

static HardwareSerial *uart_of(int port)
{
    return port == 1 ? &Serial1 : port == 2 ? &Serial2 : NULL;
}

extern "C" int bl_impl_uart_begin(int port, uint32_t baud, uint32_t config, int rx, int tx)
{
    HardwareSerial *u = uart_of(port);
    if (!u || !baud) {
        return BL_ERR_ARG;
    }
    if (rx < 0 || tx < 0) {
        if (port != 1) {
            return BL_ERR_ARG;      // Serial2 has no free default pins on this board: pass them
        }
        rx = SERIAL1_RX;
        tx = SERIAL1_TX;
    }
    if (!bl_pin_usable(rx) || !bl_pin_usable(tx)) {
        return BL_ERR_ARG;
    }
    u->begin(baud, config ? config : SERIAL_8N1, rx, tx);
    return BL_OK;
}

extern "C" void bl_impl_uart_end(int port)
{
    HardwareSerial *u = uart_of(port);
    if (u) {
        u->end();
    }
}

extern "C" int bl_impl_uart_write(int port, const uint8_t *data, size_t len)
{
    HardwareSerial *u = uart_of(port);
    return (u && data) ? (int)u->write(data, len) : 0;
}

extern "C" int bl_impl_uart_available(int port)
{
    HardwareSerial *u = uart_of(port);
    return u ? u->available() : 0;
}

extern "C" int bl_impl_uart_read(int port, uint8_t *buf, size_t size)
{
    HardwareSerial *u = uart_of(port);
    if (!u || !buf || !u->available()) {
        return 0;
    }
    return (int)u->read(buf, size);
}

extern "C" int bl_impl_uart_peek(int port)
{
    HardwareSerial *u = uart_of(port);
    return u ? u->peek() : -1;
}

extern "C" void bl_impl_uart_flush(int port)
{
    HardwareSerial *u = uart_of(port);
    if (u) {
        u->flush();
    }
}
