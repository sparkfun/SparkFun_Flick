/* API table: hardware UARTs 1 and 2 (sketch Serial1 / Serial2). Sketch Serial
 * is the console (native USB or UART0, depending on the target). */
#include <Arduino.h>
#include "gen/bl_impl.h"
#include "bl_port.h"

static HardwareSerial *uart_of(int port)
{
    if (port == 1) {
        return &Serial1;
    }
#if SOC_UART_NUM > 2    // no Serial2 on the ESP32-C3 (on the C6/C5 it is the low-power UART)
    if (port == 2) {
        return &Serial2;
    }
#endif
    return NULL;
}

extern "C" int bl_impl_uart_begin(int port, uint32_t baud, uint32_t config, int rx, int tx)
{
    HardwareSerial *u = uart_of(port);
    if (port == 2 && !u) {
        return BL_ERR_UNSUPPORTED;
    }
    if (!u || !baud) {
        return BL_ERR_ARG;
    }
    if (rx < 0 || tx < 0) {
        rx = port == 1 ? BL_SERIAL1_RX : BL_SERIAL2_RX;
        tx = port == 1 ? BL_SERIAL1_TX : BL_SERIAL2_TX;
        if (rx < 0 || tx < 0) {
            return BL_ERR_ARG;      // no free default pins on this board: pass them
        }
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

extern "C" int bl_impl_uart_set_rx_buffer(int port, size_t size)
{
    HardwareSerial *u = uart_of(port);
    if (!u) {
        return port == 2 ? BL_ERR_UNSUPPORTED : BL_ERR_ARG;
    }
    // Arduino only accepts this before begin(), and sizes above the hardware FIFO.
    return u->setRxBufferSize(size) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_uart_available_for_write(int port)
{
    HardwareSerial *u = uart_of(port);
    return u ? u->availableForWrite() : 0;
}

/* setRxFIFOFull: how many received bytes the hardware FIFO holds before the
 * driver moves them to the receive buffer (ABI 1.10). */
extern "C" int bl_impl_uart_set_rx_fifo_full(int port, uint8_t bytes)
{
    HardwareSerial *u = uart_of(port);
    if (!u || bytes == 0 || bytes > 127) {
        return BL_ERR_ARG;
    }
    return u->setRxFIFOFull(bytes) ? BL_OK : BL_ERR_FAIL;
}

/* updateBaudRate: a new baud rate for a started port, keeping its pins and
 * buffers (ABI 1.10). */
extern "C" int bl_impl_uart_set_baud(int port, uint32_t baud)
{
    HardwareSerial *u = uart_of(port);
    if (!u || baud == 0) {
        return BL_ERR_ARG;
    }
    u->updateBaudRate(baud);
    return BL_OK;
}
