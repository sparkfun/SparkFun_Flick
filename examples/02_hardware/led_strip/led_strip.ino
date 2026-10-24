// led_strip.ino - FastLED rainbow and sparkles on a WS2812 / NeoPixel strip.
// Ordinary FastLED code. As written it drives the board's own RGB LED; to use
// a strip, set DATA_PIN to the strip's data pin and NUM_LEDS to its length.
#include <FastLED.h>

#ifndef RGB_BUILTIN
#error "This board has no built-in RGB LED. Set DATA_PIN below to your strip's data pin."
#endif
#define DATA_PIN RGB_BUILTIN
#define NUM_LEDS 1

CRGB leds[NUM_LEDS];
uint8_t hue;

void setup()
{
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(48);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
}

void loop()
{
    fill_rainbow(leds, NUM_LEDS, hue, 7);
    if (random8() < 40) {
        leds[random16(NUM_LEDS)] += CRGB::White;    // sparkle
    }
    FastLED.show();
    EVERY_N_MILLISECONDS(20) { hue++; }
    delay(10);
}
