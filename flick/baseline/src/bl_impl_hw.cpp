/* API table: touch, I2S, LED strips, LEDC PWM, DNS server (ABI 1.6). */
#include <Arduino.h>
#include <ESP_I2S.h>
#include <DNSServer.h>
#include "esp32-hal-rmt.h"
#include "freertos/queue.h"
#include "soc/soc_caps.h"
#include "gen/bl_impl.h"
#include "bl_port.h"
#include "bl_ledc.h"
#include "sketch_loader.h"

static bool output_pin_ok(int pin)
{
    return bl_pin_usable(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

/* ---------------- touch ---------------- */

#if SOC_TOUCH_SENSOR_SUPPORTED
static QueueHandle_t s_touch_q;

static void IRAM_ATTR touch_isr(void *arg)
{
    uint8_t pin = (uint8_t)(intptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_touch_q, &pin, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

extern "C" uint32_t bl_impl_touch_read(int pin)
{
    return bl_pin_usable(pin) ? (uint32_t)touchRead((uint8_t)pin) : 0;
}

extern "C" int bl_impl_touch_attach(int pin, uint32_t threshold)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    if (!s_touch_q) {
        s_touch_q = xQueueCreate(16, sizeof(uint8_t));
    }
    touchAttachInterruptArg((uint8_t)pin, touch_isr, (void *)(intptr_t)pin, (touch_value_t)threshold);
    return BL_OK;
}

extern "C" int bl_impl_touch_detach(int pin)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    touchDetachInterrupt((uint8_t)pin);
    return BL_OK;
}

extern "C" int bl_impl_touch_take(void)
{
    uint8_t pin;
    return (s_touch_q && xQueueReceive(s_touch_q, &pin, 0) == pdTRUE) ? pin : -1;
}
#else
extern "C" uint32_t bl_impl_touch_read(int pin) { return 0; }
extern "C" int bl_impl_touch_attach(int pin, uint32_t threshold) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_touch_detach(int pin) { return BL_ERR_UNSUPPORTED; }
extern "C" int bl_impl_touch_take(void) { return -1; }
#endif

/* ---------------- I2S (one port) ---------------- */

static I2SClass *s_i2s;

extern "C" void bl_impl_i2s_end(void)
{
    if (s_i2s) {
        s_i2s->end();
        delete s_i2s;
        s_i2s = NULL;
    }
}

extern "C" int bl_impl_i2s_begin(int mode, uint32_t rate, int bits, int stereo, int bclk, int ws, int dout, int din, int mclk)
{
    bl_impl_i2s_end();
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        return BL_ERR_ARG;
    }
    s_i2s = new I2SClass();
    i2s_mode_t m;
    switch (mode) {
    case BL_I2S_STD:
        m = I2S_MODE_STD;
        s_i2s->setPins(bclk, ws, dout, din, mclk);
        break;
#if SOC_I2S_SUPPORTS_TDM
    case BL_I2S_TDM:
        m = I2S_MODE_TDM;
        s_i2s->setPins(bclk, ws, dout, din, mclk);
        break;
#endif
#if SOC_I2S_SUPPORTS_PDM_TX
    case BL_I2S_PDM_TX:
        m = I2S_MODE_PDM_TX;
        s_i2s->setPinsPdmTx(bclk, dout);
        break;
#endif
#if SOC_I2S_SUPPORTS_PDM_RX
    case BL_I2S_PDM_RX:
        m = I2S_MODE_PDM_RX;
        s_i2s->setPinsPdmRx(bclk, din);
        break;
#endif
    default:
        bl_impl_i2s_end();
        return BL_ERR_UNSUPPORTED;
    }
    if (!s_i2s->begin(m, rate, (i2s_data_bit_width_t)bits, stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO)) {
        bl_impl_i2s_end();
        return BL_ERR_FAIL;
    }
    return BL_OK;
}

extern "C" int bl_impl_i2s_write(const uint8_t *data, size_t len)
{
    return (s_i2s && data) ? (int)s_i2s->write(data, len) : 0;
}

extern "C" int bl_impl_i2s_read(uint8_t *buf, size_t size)
{
    return (s_i2s && buf) ? (int)s_i2s->readBytes((char *)buf, size) : 0;
}

/* ---------------- LED strips (RMT, 10 MHz = 100 ns ticks) ---------------- */

struct StripTiming {
    uint16_t t0h, t0l, t1h, t1l;    // ticks
};
static const StripTiming k_timing[] = {
    { 4, 8, 8, 4 },         // BL_STRIP_WS2812: 0.4/0.8 us, 0.8/0.4 us (as Arduino's rgbLedWrite)
    { 3, 9, 6, 6 },         // BL_STRIP_SK6812: 0.3/0.9 us, 0.6/0.6 us
    { 5, 20, 12, 13 },      // BL_STRIP_WS2811 (400 kHz): 0.5/2.0 us, 1.2/1.3 us
};
#ifndef BL_PORT_LED_STRIP
static uint64_t s_rmt_pins;     // pins already set up for RMT
#endif

extern "C" int bl_impl_led_strip_write(int pin, int chipset, const uint8_t *data, size_t len)
{
    if (!output_pin_ok(pin) || pin >= 64 || chipset < 0 || chipset > BL_STRIP_WS2811 || (!data && len)) {
        return BL_ERR_ARG;
    }
#ifdef BL_PORT_LED_STRIP
    // This target bit-bangs strips instead of using RMT (see its bl_port.h).
    const StripTiming &pt = k_timing[chipset];
    const uint8_t timing[4] = { (uint8_t)pt.t0h, (uint8_t)pt.t0l, (uint8_t)pt.t1h, (uint8_t)pt.t1l };
    if (!len) {
        return BL_OK;
    }
    return bl_port_led_strip_write(pin, data, len, timing) ? BL_OK : BL_ERR_NO_MEM;
#else
    if (!(s_rmt_pins & (1ULL << pin))) {
        if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
            return BL_ERR_FAIL;
        }
        s_rmt_pins |= 1ULL << pin;
    }
    if (!len) {
        return BL_OK;
    }
    rmt_data_t *sym = (rmt_data_t *)malloc(len * 8 * sizeof(rmt_data_t));
    if (!sym) {
        return BL_ERR_NO_MEM;
    }
    const StripTiming &t = k_timing[chipset];
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--, n++) {
            bool one = (data[i] >> bit) & 1;
            sym[n].level0 = 1;
            sym[n].duration0 = one ? t.t1h : t.t0h;
            sym[n].level1 = 0;
            sym[n].duration1 = one ? t.t1l : t.t0l;
        }
    }
    bool ok;
    {
        WdtPause wdt;
        ok = rmtWrite(pin, sym, n, RMT_WAIT_FOR_EVER);
    }
    free(sym);
    delayMicroseconds(80);      // latch: the line must stay low > 50 us before the next frame
    return ok ? BL_OK : BL_ERR_FAIL;
#endif
}

/* ---------------- LEDC: PWM at any frequency (servos, tones) ---------------- */

extern "C" int bl_impl_ledc_attach(int pin, uint32_t freq, int resolution)
{
    return output_pin_ok(pin) && ledcAttach((uint8_t)pin, freq, (uint8_t)resolution) ? 1 : 0;
}

extern "C" int bl_impl_ledc_write(int pin, uint32_t duty)
{
    if (!output_pin_ok(pin)) {
        return 0;
    }
    bl_ledc_settle(pin);
    return ledcWrite((uint8_t)pin, duty) ? 1 : 0;
}

extern "C" int bl_impl_ledc_detach(int pin)
{
    return output_pin_ok(pin) && ledcDetach((uint8_t)pin) ? 1 : 0;
}

extern "C" uint32_t bl_impl_ledc_write_tone(int pin, uint32_t freq)
{
    if (!output_pin_ok(pin)) {
        return 0;
    }
    bl_ledc_settle(pin);
    return ledcWriteTone((uint8_t)pin, freq);
}

extern "C" uint32_t bl_impl_ledc_change_frequency(int pin, uint32_t freq, int resolution)
{
    return output_pin_ok(pin) ? ledcChangeFrequency((uint8_t)pin, freq, (uint8_t)resolution) : 0;
}

/* ---------------- DNS server (captive portals) ---------------- */

static DNSServer *s_dns;

extern "C" int bl_impl_dns_start(uint16_t port, const char *domain, uint32_t ip)
{
    if (!s_dns) {
        s_dns = new DNSServer();
    }
    s_dns->stop();
    return s_dns->start(port ? port : 53, String(domain && *domain ? domain : "*"), IPAddress(ip)) ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_dns_stop(void)
{
    if (s_dns) {
        s_dns->stop();
    }
}

extern "C" void bl_impl_dns_error_reply(int code)
{
    if (!s_dns) {
        s_dns = new DNSServer();
    }
    s_dns->setErrorReplyCode((DNSReplyCode)code);
}
