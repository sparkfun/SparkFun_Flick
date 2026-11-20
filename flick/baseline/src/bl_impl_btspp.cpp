/* API table: Bluetooth Classic serial port (SPP, Arduino's BluetoothSerial).
 * Only chips with Bluetooth Classic have it (the classic ESP32). Elsewhere the
 * same table entries exist and report BL_ERR_UNSUPPORTED.
 *
 * Buffers (ABI 1.10): received bytes go through BluetoothSerial::onData() into
 * a receive buffer of the size the sketch asks for (Arduino-ESP32's own is 512
 * bytes). With a send buffer, write() copies into it and a small task feeds
 * BluetoothSerial, so the sketch doesn't wait on the stack's 32-packet queue. */
#include <Arduino.h>
#include "sdkconfig.h"
#include "gen/bl_impl.h"
#include "sketch_loader.h"
#include "bl_console.h"

#if defined(CONFIG_BT_SPP_ENABLED)
#include <BluetoothSerial.h>
#include "esp_heap_caps.h"
#include "freertos/stream_buffer.h"

#define RX_DEFAULT  512
#define TX_CHUNK    330     /* BluetoothSerial's packet size */

static BluetoothSerial *s_bt;
static bool s_started;
static String s_pin;
static size_t s_rx_size = RX_DEFAULT;
static size_t s_tx_size;
static bool s_ssp, s_ssp_input, s_ssp_output;

static StreamBufferHandle_t s_rx, s_tx;
static int s_peek = -1;         /* a byte peek() took out of s_rx */
static TaskHandle_t s_tx_task;

/* A stream buffer whose storage is in PSRAM when there is some. */
static StreamBufferHandle_t buffer_new(size_t size)
{
    uint8_t *mem = (uint8_t *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        mem = (uint8_t *)malloc(size + 1);
    }
    StaticStreamBuffer_t *ctl = (StaticStreamBuffer_t *)malloc(sizeof(StaticStreamBuffer_t));
    if (!mem || !ctl) {
        free(mem);
        free(ctl);
        return NULL;
    }
    return xStreamBufferCreateStatic(size, 1, mem, ctl);
}

static void on_data(const uint8_t *data, size_t len)
{
    xStreamBufferSend(s_rx, data, len, 0);      // full: the rest is dropped, as Arduino's queue does
}

static void tx_task(void *)
{
    static uint8_t chunk[TX_CHUNK];
    for (;;) {
        size_t n = xStreamBufferReceive(s_tx, chunk, sizeof(chunk), portMAX_DELAY);
        if (n && s_started) {
            s_bt->write(chunk, n);
        }
    }
}

extern "C" int bl_impl_bt_spp_config(size_t rx_size, size_t tx_size)
{
    if (s_started || s_rx) {
        return BL_ERR_STATE;    // the buffers are made at the first begin
    }
    if ((rx_size && rx_size < 64) || rx_size > 256 * 1024 || tx_size > 256 * 1024) {
        return BL_ERR_ARG;
    }
    s_rx_size = rx_size ? rx_size : RX_DEFAULT;
    s_tx_size = tx_size;
    return BL_OK;
}

extern "C" int bl_impl_bt_spp_ssp(int enable, int input, int output)
{
    if (s_started) {
        return BL_ERR_STATE;
    }
    s_ssp = enable != 0;
    s_ssp_input = input != 0;
    s_ssp_output = output != 0;
    return BL_OK;
}

extern "C" int bl_impl_bt_spp_begin(const char *name)
{
    if (!s_bt) {
        s_bt = new BluetoothSerial();
    }
    if (!s_started && btMemReleased(BT_MODE_CLASSIC_BT)) {
        // BLE started first and gave Bluetooth Classic's memory to the heap.
        BLOG_E("Bluetooth Classic is unavailable after BLE has started; restart, then begin SPP first");
        return BL_ERR_FAIL;
    }
    if (s_started) {
        return BL_OK;
    }
    if (!s_rx) {                // kept across end()/begin()
        s_rx = buffer_new(s_rx_size);
        if (!s_rx) {
            return BL_ERR_NO_MEM;
        }
    }
    if (s_tx_size && !s_tx) {
        s_tx = buffer_new(s_tx_size);
        if (!s_tx || xTaskCreatePinnedToCore(tx_task, "bt_spp_tx", 3072, NULL, 5, &s_tx_task, tskNO_AFFINITY) != pdPASS) {
            return BL_ERR_NO_MEM;
        }
    }
    s_bt->onData(on_data);
    if (s_ssp) {
        s_bt->enableSSP(s_ssp_input, s_ssp_output);
    }
    {
        WdtPause wdt;   // starting the Bluetooth stack takes about a second
        s_started = s_bt->begin(String(name && *name ? name : "ESP32"));
    }
    if (s_started && s_pin.length()) {
        s_bt->setPin(s_pin.c_str(), (uint8_t)s_pin.length());
    }
    return s_started ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_bt_spp_end(void)
{
    if (s_started) {
        s_started = false;
        s_bt->end();
        xStreamBufferReset(s_rx);
        s_peek = -1;
        if (s_tx) {
            xStreamBufferReset(s_tx);
        }
    }
}

extern "C" int bl_impl_bt_spp_connected(void)
{
    return s_started && s_bt->hasClient() ? 1 : 0;
}

extern "C" int bl_impl_bt_spp_write(const uint8_t *data, size_t len)
{
    if (!s_started || !data) {
        return 0;
    }
    if (s_tx) {
        return (int)xStreamBufferSend(s_tx, data, len, 0);     // what fits; the task sends it
    }
    return (int)s_bt->write(data, len);
}

extern "C" int bl_impl_bt_spp_available(void)
{
    if (!s_started) {
        return 0;
    }
    return (int)xStreamBufferBytesAvailable(s_rx) + (s_peek >= 0 ? 1 : 0);
}

extern "C" int bl_impl_bt_spp_read(uint8_t *buf, size_t size)
{
    if (!s_started || !buf || !size) {
        return 0;
    }
    size_t n = 0;
    if (s_peek >= 0) {
        buf[n++] = (uint8_t)s_peek;
        s_peek = -1;
    }
    return (int)(n + xStreamBufferReceive(s_rx, buf + n, size - n, 0));
}

extern "C" int bl_impl_bt_spp_peek(void)
{
    if (!s_started) {
        return -1;
    }
    if (s_peek < 0) {
        uint8_t b;
        if (xStreamBufferReceive(s_rx, &b, 1, 0) == 1) {
            s_peek = b;
        }
    }
    return s_peek;
}

extern "C" void bl_impl_bt_spp_flush(void)
{
    if (!s_started) {
        return;
    }
    WdtPause wdt;
    while (s_tx && !xStreamBufferIsEmpty(s_tx) && s_bt->hasClient()) {
        delay(5);
    }
    s_bt->flush();
}

extern "C" int bl_impl_bt_spp_set_pin(const char *pin)
{
    if (!pin || strlen(pin) > 16) {
        return BL_ERR_ARG;
    }
    s_pin = pin;
    if (s_started) {
        s_bt->setPin(s_pin.c_str(), (uint8_t)s_pin.length());
    }
    return BL_OK;
}

extern "C" int bl_impl_bt_spp_confirm(int accept)
{
    if (!s_started) {
        return BL_ERR_STATE;
    }
    s_bt->confirmReply(accept != 0);
    return BL_OK;
}

extern "C" int bl_impl_bt_spp_disconnect(void)
{
    return s_started && s_bt->hasClient() && s_bt->disconnect() ? 1 : 0;
}

extern "C" int bl_impl_bt_spp_unpair_all(void)
{
    if (!s_started) {
        return BL_ERR_STATE;
    }
    s_bt->deleteAllBondedDevices();
    return BL_OK;
}

#else   /* no Bluetooth Classic on this chip */

extern "C" int bl_impl_bt_spp_begin(const char *name) { return BL_ERR_UNSUPPORTED; }
extern "C" void bl_impl_bt_spp_end(void) {}
extern "C" int bl_impl_bt_spp_connected(void) { return 0; }
extern "C" int bl_impl_bt_spp_write(const uint8_t *data, size_t len) { return 0; }
extern "C" int bl_impl_bt_spp_available(void) { return 0; }
extern "C" int bl_impl_bt_spp_read(uint8_t *buf, size_t size) { return 0; }
extern "C" int bl_impl_bt_spp_peek(void) { return -1; }
extern "C" void bl_impl_bt_spp_flush(void) {}
extern "C" int bl_impl_bt_spp_set_pin(const char *pin) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_bt_spp_config(size_t rx_size, size_t tx_size) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_bt_spp_ssp(int enable, int input, int output) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_bt_spp_confirm(int accept) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_bt_spp_disconnect(void) { return 0; }
extern "C" int bl_impl_bt_spp_unpair_all(void) { return BL_ERR_UNSUPPORTED; }

#endif
