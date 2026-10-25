/* API table: ADC resolution/attenuation, DAC, chip temperature, reset reason
 * and the last crash (ABI 1.6). */
#include <Arduino.h>
#include <math.h>
#include "esp_system.h"
#include "esp_core_dump.h"
#include "soc/soc_caps.h"
#include "gen/bl_impl.h"
#include "bl_port.h"

/* ---- ADC ---- */

extern "C" int bl_impl_adc_set_resolution(int bits)
{
    if (bits < 1 || bits > 16) {    // Arduino scales results above or below the native 12 bits
        return BL_ERR_ARG;
    }
    analogReadResolution((uint8_t)bits);
    return BL_OK;
}

extern "C" int bl_impl_adc_set_attenuation(int pin, int atten)
{
    if (atten < ADC_0db || atten > ADC_11db) {     // BL_ADC_ATTEN_* match adc_attenuation_t
        return BL_ERR_ARG;
    }
    if (pin < 0) {
        analogSetAttenuation((adc_attenuation_t)atten);
        return BL_OK;
    }
    if (!bl_pin_is_adc(pin)) {
        return BL_ERR_ARG;
    }
    analogSetPinAttenuation((uint8_t)pin, (adc_attenuation_t)atten);
    return BL_OK;
}

/* ---- DAC (classic ESP32: GPIO 25 and 26) ---- */

extern "C" int bl_impl_dac_write(int pin, uint8_t value)
{
#if SOC_DAC_SUPPORTED
    if (pin != 25 && pin != 26) {
        return BL_ERR_ARG;
    }
    dacWrite((uint8_t)pin, value);
    return BL_OK;
#else
    (void)pin;
    (void)value;
    return BL_ERR_UNSUPPORTED;
#endif
}

/* ---- Chip temperature ---- */

extern "C" float bl_impl_temperature_read(void)
{
#if SOC_TEMP_SENSOR_SUPPORTED
    return temperatureRead();
#else
    return NAN;
#endif
}

/* ---- Reset reason and last crash ---- */

extern "C" int bl_impl_reset_reason(void)
{
    return (int)esp_reset_reason();
}

extern "C" int bl_impl_last_crash(char *buf, size_t size)
{
    if (!buf || !size) {
        return BL_ERR_ARG;
    }
    buf[0] = '\0';
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    if (esp_core_dump_image_check() != ESP_OK) {
        return 0;       // no crash recorded (or the coredump partition was erased)
    }
    esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *)malloc(sizeof(*sum));
    if (!sum) {
        return BL_ERR_NO_MEM;
    }
    int n = 0;
    if (esp_core_dump_get_summary(sum) == ESP_OK) {
        sum->exc_task[sizeof(sum->exc_task) - 1] = '\0';
#if __XTENSA__
        n = snprintf(buf, size, "task=%s pc=0x%08lx cause=%lu addr=0x%08lx bt=", sum->exc_task,
                     (unsigned long)sum->exc_pc, (unsigned long)sum->ex_info.exc_cause,
                     (unsigned long)sum->ex_info.exc_vaddr);
        for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 8 && n > 0 && (size_t)n < size; i++) {
            n += snprintf(buf + n, size - n, i ? ",0x%08lx" : "0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
        }
#else
        n = snprintf(buf, size, "task=%s pc=0x%08lx mcause=%lu mtval=0x%08lx ra=0x%08lx", sum->exc_task,
                     (unsigned long)sum->exc_pc, (unsigned long)sum->ex_info.mcause,
                     (unsigned long)sum->ex_info.mtval, (unsigned long)sum->ex_info.ra);
#endif
    }
    free(sum);
    if (n < 0) {
        n = 0;
    }
    return (size_t)n < size ? n : (int)size - 1;
#else
    return 0;
#endif
}

/* Forget the recorded crash (loader command CRASHCLEAR). */
extern "C" void bl_last_crash_clear(void)
{
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    esp_core_dump_image_erase();
#endif
}

/* ---- Teensy-only entries (ABI 1.8): sketch interrupt handlers, DMA channels
 * and the Audio library are i.MX RT1062 features; on the ESP32 family they
 * answer BL_ERR_UNSUPPORTED, and attachInterrupt stays deferred. ---- */
#define BL_STUB_TEENSY 1
#include "gen/bl_impl_stubs.inc"
