/* sketch_teensy.cpp - Teensy 4.x objects (baseline_teensy.h): the extra UARTs and Wire2. */
#include "baseline.h"

HardwareSerial Serial3(3);
HardwareSerial Serial4(4);
HardwareSerial Serial5(5);
HardwareSerial Serial6(6);
HardwareSerial Serial7(7);
HardwareSerial Serial8(8);      /* Teensy 4.1 only: begin() fails on the 4.0 */
TwoWire Wire2(2);

/* ---- analogWrite with analogWriteResolution() / analogWriteFrequency() ----
 * Plain 8-bit writes at the pin's default frequency are pwm_write(); anything
 * else goes through Baseline's LEDC calls, which set Teensy's per-pin
 * frequency and the resolution of each write. */
#define MAX_PINS 64

static uint8_t s_bits = 8;
static float s_freq[MAX_PINS];      /* 0: Teensy's default */
static uint8_t s_attached[MAX_PINS];

void analogWriteResolution(unsigned int bits)
{
    if (bits >= 1 && bits <= 16) {
        s_bits = (uint8_t)bits;
    }
}

void analogWriteFrequency(uint8_t pin, float frequency)
{
    if (pin < MAX_PINS && frequency > 0) {
        s_freq[pin] = frequency;
        s_attached[pin] = 0;        /* attach again with the new frequency */
    }
}

int bl_teensy_analog_write(int pin, int value)
{
    if (pin < 0 || pin >= MAX_PINS) {
        return BL_ERR_ARG;
    }
    if (s_bits == 8 && s_freq[pin] == 0) {
        return BL->pwm_write(pin, value);
    }
    if (s_attached[pin] != s_bits) {
        if (bl_ledc_attach(pin, s_freq[pin] > 0 ? (uint32_t)s_freq[pin] : 4482, s_bits) != 1) {
            return BL_ERR_ARG;
        }
        s_attached[pin] = s_bits;
    }
    return bl_ledc_write(pin, value < 0 ? 0 : (uint32_t)value) == 1 ? BL_OK : BL_ERR_FAIL;
}
