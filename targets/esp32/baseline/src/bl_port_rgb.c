/*
 * bl_port_rgb.c - WS2812-type LEDs for the classic ESP32, bit-banged:
 * rgbLedWrite() (one LED) and led_strip_write() (any strip).
 *
 * Arduino's rgbLedWrite() and Baseline's other targets use the RMT driver, and
 * linking it puts its TX and RX ISRs (~1.9 KB) in IRAM. With WiFi, BLE,
 * Bluetooth Classic and the PSRAM driver, the ESP32's 128 KB IRAM has no room
 * for that. This sends the same waveform from IRAM, timed by the CPU cycle
 * counter, with interrupts off on this core while the frame goes out
 * (about 30 us per RGB LED). The sketch task runs on CPU1, so WiFi and
 * Bluetooth on CPU0 keep running.
 */
#include <Arduino.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "bl_port.h"

static portMUX_TYPE s_rgb_mux = portMUX_INITIALIZER_UNLOCKED;

/* IRAM so a cache miss cannot stretch a pulse; `data` must be in internal RAM.
 * noinline: see bl_port_mmu.c. Times are in CPU cycles. */
static void IRAM_ATTR __attribute__((noinline))
send_bytes(uint32_t set_reg, uint32_t clr_reg, uint32_t mask, const uint8_t *data, size_t len,
           uint32_t t0h, uint32_t t0p, uint32_t t1h, uint32_t t1p)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t byte = data[i];
        for (uint32_t bit = 0x80; bit; bit >>= 1) {
            uint32_t high = (byte & bit) ? t1h : t0h;
            uint32_t period = (byte & bit) ? t1p : t0p;
            uint32_t start = esp_cpu_get_cycle_count();
            REG_WRITE(set_reg, mask);
            while (esp_cpu_get_cycle_count() - start < high) {
            }
            REG_WRITE(clr_reg, mask);
            while (esp_cpu_get_cycle_count() - start < period) {
            }
        }
    }
}

/* timing = { t0h, t0l, t1h, t1l } in 0.1 us. */
static void send_frame(int pin, const uint8_t *data, size_t len, const uint8_t timing[4])
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);

    uint32_t mhz = esp_rom_get_cpu_ticks_per_us();
    uint32_t set_reg = pin < 32 ? GPIO_OUT_W1TS_REG : GPIO_OUT1_W1TS_REG;
    uint32_t clr_reg = pin < 32 ? GPIO_OUT_W1TC_REG : GPIO_OUT1_W1TC_REG;
    uint32_t mask = 1u << (pin & 31);

    portENTER_CRITICAL(&s_rgb_mux);
    send_bytes(set_reg, clr_reg, mask, data, len,
               mhz * timing[0] / 10, mhz * (timing[0] + timing[1]) / 10,
               mhz * timing[2] / 10, mhz * (timing[2] + timing[3]) / 10);
    portEXIT_CRITICAL(&s_rgb_mux);
    esp_rom_delay_us(80);   // latch: > 50 us low before the next frame
}

void bl_port_rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)
{
    static const uint8_t ws2812[4] = { 4, 8, 8, 4 };
    uint8_t grb[3] = { green, red, blue };
    send_frame(pin, grb, sizeof grb, ws2812);
}

bool bl_port_led_strip_write(int pin, const uint8_t *data, size_t len, const uint8_t timing[4])
{
    /* The sketch's buffer may be in PSRAM or flash, which a cache miss would
     * make too slow: send from an internal-RAM copy. */
    uint8_t *buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        return false;
    }
    memcpy(buf, data, len);
    send_frame(pin, buf, len, timing);
    free(buf);
    return true;
}
