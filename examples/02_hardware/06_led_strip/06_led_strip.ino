// 06_led_strip.ino - FastLED rainbow and sparkles on a WS2812 / NeoPixel strip.
// Ordinary FastLED code. On a board with its own RGB LED it drives that; on
// others, a strip's data line on pin 2. Set DATA_PIN and NUM_LEDS for your strip.
#include <FastLED.h>

#ifdef RGB_BUILTIN
#define DATA_PIN RGB_BUILTIN
#define NUM_LEDS 1
#else
#define DATA_PIN 2
#define NUM_LEDS 8
#endif

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
