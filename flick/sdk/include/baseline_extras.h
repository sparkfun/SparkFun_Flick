/*
 * baseline_extras.h - smaller Arduino-ESP32 calls that libraries and examples
 * expect: esp_reset_reason(), analog attenuation, the board's A1..A7, pulseIn()
 * and WiFiMulti. Included by baseline_arduino.h (WiFi events are in baseline_net.h).
 */
#ifndef BASELINE_EXTRAS_H
#define BASELINE_EXTRAS_H

#ifdef __cplusplus

#if defined(BL_PLATFORM_ESP32)
/* ---- Reset reason (ESP-IDF esp_reset_reason_t values) ---- */
typedef enum {
    ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO, ESP_RST_USB,
    ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;
static inline esp_reset_reason_t esp_reset_reason(void) { return (esp_reset_reason_t)bl_reset_reason(); }

/* ---- ADC input range ---- */
typedef enum { ADC_0db = BL_ADC_ATTEN_0DB, ADC_2_5db = BL_ADC_ATTEN_2_5DB, ADC_6db = BL_ADC_ATTEN_6DB,
               ADC_11db = BL_ADC_ATTEN_11DB } adc_attenuation_t;
static inline void analogSetAttenuation(adc_attenuation_t atten) { bl_adc_set_attenuation(-1, atten); }
static inline void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t atten) { bl_adc_set_attenuation(pin, atten); }

#endif /* BL_PLATFORM_ESP32 */
/* ---- More analog pins, where the board's bl_board.h names them ---- */
#ifdef BL_BOARD_A1
static const int A1 = BL_BOARD_A1;
#endif
#ifdef BL_BOARD_A2
static const int A2 = BL_BOARD_A2;
#endif
#ifdef BL_BOARD_A3
static const int A3 = BL_BOARD_A3;
#endif
#ifdef BL_BOARD_A4
static const int A4 = BL_BOARD_A4;
#endif
#ifdef BL_BOARD_A5
static const int A5 = BL_BOARD_A5;
#endif
#ifdef BL_BOARD_A6
static const int A6 = BL_BOARD_A6;
#endif
#ifdef BL_BOARD_A7
static const int A7 = BL_BOARD_A7;
#endif

/* ---- pulseIn: length of a pulse in microseconds (0 on timeout) ----
 * Polls the pin through Baseline, so pulses shorter than ~10 us are not
 * measured reliably. */
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout = 1000000UL);
static inline unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout = 1000000UL)
{
    return pulseIn(pin, state, timeout);
}

#if defined(BL_HAS_WIFI)
/* ---- WiFiMulti: connect to the strongest of several known networks ---- */
class WiFiMulti {
public:
    bool addAP(const char *ssid, const char *password = nullptr);
    uint8_t run(uint32_t connectTimeout = 5000);    // returns WiFi.status()

private:
    static const int MAX_APS = 8;
    const char *ssid_[MAX_APS] = {};
    const char *pass_[MAX_APS] = {};
    int count_ = 0;
};

#endif /* BL_HAS_WIFI */
#endif /* __cplusplus */
#endif /* BASELINE_EXTRAS_H */
