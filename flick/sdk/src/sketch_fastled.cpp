/* sketch_fastled.cpp - FastLED-compatible API (see baseline_fastled.h). */
#include "baseline.h"

#if defined(BL_PLATFORM_ESP32) || defined(BL_PLATFORM_RP2)     /* the whole file: boards with this feature only */

CFastLED FastLED;

/* ---- sine: quarter-wave table, linear interpolation ---- */

static const int16_t k_quarter_sine[17] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170,
    25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767,
};

int16_t sin16(uint16_t theta)
{
    uint16_t q = theta >> 14;           // quadrant
    uint16_t t = theta & 0x3FFF;        // position inside it (0..16383)
    if (q & 1) {
        t = 16384 - t;
    }
    uint16_t i = t >> 10;               // table step: 1024 units each
    uint16_t f = t & 0x3FF;
    int32_t v = k_quarter_sine[i];
    if (i < 16) {
        v += ((int32_t)(k_quarter_sine[i + 1] - k_quarter_sine[i]) * f) >> 10;
    }
    return (int16_t)(q & 2 ? -v : v);
}

/* ---- HSV -> RGB, FastLED's "rainbow" mapping (even yellow, 8 sections) ---- */

void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb)
{
    uint8_t hue = hsv.hue, sat = hsv.sat, val = hsv.val;
    uint8_t offset8 = (hue & 0x1F) << 3;
    uint8_t third = scale8(offset8, 85);
    uint8_t twothirds = scale8(offset8, 170);
    uint8_t r, g, b;
    switch (hue >> 5) {
    case 0:  r = 255 - third; g = third; b = 0; break;                     // red -> orange
    case 1:  r = 171; g = 85 + third; b = 0; break;                        // orange -> yellow
    case 2:  r = 171 - twothirds; g = 170 + third; b = 0; break;           // yellow -> green
    case 3:  r = 0; g = 255 - offset8; b = offset8; break;                 // green -> aqua
    case 4:  r = 0; g = 171 - twothirds; b = 85 + twothirds; break;        // aqua -> blue
    case 5:  r = third; g = 0; b = 255 - third; break;                     // blue -> purple
    case 6:  r = 85 + third; g = 0; b = 171 - third; break;                // purple -> pink
    default: r = 170 + third; g = 0; b = 85 - third; break;                // pink -> red
    }
    if (sat != 255) {
        if (sat == 0) {
            r = g = b = 255;
        } else {
            uint8_t desat = scale8_video(255 - sat, 255 - sat);
            uint8_t satscale = 255 - desat;
            r = scale8(r, satscale) + desat;
            g = scale8(g, satscale) + desat;
            b = scale8(b, satscale) + desat;
        }
    }
    if (val != 255) {
        val = scale8_video(val, val);   // perceptual dimming curve
        r = scale8(r, val);
        g = scale8(g, val);
        b = scale8(b, val);
    }
    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
}

/* ---- fills ---- */

void fill_solid(CRGB *leds, int n, const CRGB &color)
{
    for (int i = 0; i < n; i++) {
        leds[i] = color;
    }
}

void fill_rainbow(CRGB *leds, int n, uint8_t initialhue, uint8_t deltahue)
{
    CHSV hsv(initialhue, 240, 255);
    for (int i = 0; i < n; i++) {
        leds[i] = hsv;
        hsv.hue += deltahue;
    }
}

void fill_rainbow_circular(CRGB *leds, int n, uint8_t initialhue, bool reversed)
{
    if (n <= 0) {
        return;
    }
    uint16_t step = 65535 / n, hue16 = (uint16_t)initialhue << 8;
    for (int i = 0; i < n; i++) {
        leds[i] = CHSV(hue16 >> 8, 240, 255);
        hue16 = reversed ? hue16 - step : hue16 + step;
    }
}

void fill_gradient_RGB(CRGB *leds, uint16_t n, const CRGB &c1, const CRGB &c2)
{
    for (uint16_t i = 0; i < n; i++) {
        leds[i] = blend(c1, c2, n > 1 ? (uint8_t)((uint32_t)i * 255 / (n - 1)) : 0);
    }
}

void nscale8(CRGB *leds, uint16_t n, uint8_t scale)
{
    for (uint16_t i = 0; i < n; i++) {
        leds[i].nscale8(scale);
    }
}

void nscale8_video(CRGB *leds, uint16_t n, uint8_t scale)
{
    for (uint16_t i = 0; i < n; i++) {
        leds[i].nscale8_video(scale);
    }
}

void fadeToBlackBy(CRGB *leds, uint16_t n, uint8_t fadeBy) { nscale8(leds, n, 255 - fadeBy); }
void fade_raw(CRGB *leds, uint16_t n, uint8_t fadeBy) { nscale8(leds, n, 255 - fadeBy); }

void blur1d(CRGB *leds, uint16_t n, fract8 amount)
{
    uint8_t keep = 255 - amount, seep = amount >> 1;
    CRGB carryover = CRGB::Black;
    for (uint16_t i = 0; i < n; i++) {
        CRGB cur = leds[i];
        CRGB part = cur;
        part.nscale8(seep);
        cur.nscale8(keep);
        cur += carryover;
        if (i) {
            leds[i - 1] += part;
        }
        leds[i] = cur;
        carryover = part;
    }
}

/* ---- output ---- */

CLEDController &CFastLED::add(int pin, int chipset, EOrder order, CRGB *data, int nLedsOrOffset, int nLedsIfOffset)
{
    int offset = nLedsIfOffset > 0 ? nLedsOrOffset : 0;
    int n = nLedsIfOffset > 0 ? nLedsIfOffset : nLedsOrOffset;
    CLEDController &c = controllers_[count_ < MAX_CONTROLLERS ? count_++ : MAX_CONTROLLERS - 1];
    c.leds_ = data + offset;
    c.count_ = n;
    c.pin_ = (int8_t)pin;
    c.chipset_ = (uint8_t)chipset;
    c.order_ = (uint16_t)order;
    bl_led_strip_write(pin, chipset, 0, 0);     // claim the pin now, like FastLED
    return c;
}

static void send(CLEDController &c, const CRGB *src, bool solid, uint8_t brightness)
{
    if (!c.count_ || c.pin_ < 0) {
        return;
    }
    size_t len = (size_t)c.count_ * 3;
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        return;
    }
    // EOrder holds the channel for each wire position as octal digits (GRB = 0102).
    uint8_t ch0 = (c.order_ >> 6) & 3, ch1 = (c.order_ >> 3) & 3, ch2 = c.order_ & 3;
    for (int i = 0; i < c.count_; i++) {
        const CRGB &p = solid ? src[0] : src[i];
        buf[i * 3 + 0] = scale8_video(p.raw[ch0], brightness);
        buf[i * 3 + 1] = scale8_video(p.raw[ch1], brightness);
        buf[i * 3 + 2] = scale8_video(p.raw[ch2], brightness);
    }
    bl_led_strip_write(c.pin_, c.chipset_, buf, len);
    free(buf);
}

void CLEDController::showLeds(uint8_t brightness)
{
    if (leds_) {
        send(*this, leds_, false, brightness);
    }
}

void CLEDController::showColor(const CRGB &color, uint8_t brightness)
{
    send(*this, &color, true, brightness);
}

/* setMaxPower...: estimate ~20 mA per fully-lit color channel plus 1 mA idle
 * per LED, and dim the whole frame to stay under the budget. */
uint8_t CFastLED::powerLimitedBrightness(uint8_t scale)
{
    if (!maxMilliwatts_) {
        return scale;
    }
    uint32_t sum = 0, leds = 0;
    for (int c = 0; c < count_; c++) {
        CLEDController &k = controllers_[c];
        for (int i = 0; k.leds_ && i < k.count_; i++) {
            sum += k.leds_[i].r + k.leds_[i].g + k.leds_[i].b;
        }
        leds += k.count_;
    }
    uint32_t mw = (sum * scale / 255) * 20 * 5 / 255 + leds * 5;    // at 5 V
    if (mw <= maxMilliwatts_) {
        return scale;
    }
    return (uint8_t)((uint32_t)scale * maxMilliwatts_ / mw);
}

void CFastLED::show(uint8_t scale)
{
    uint8_t b = powerLimitedBrightness(scale);
    for (int c = 0; c < count_; c++) {
        controllers_[c].showLeds(b);
    }
}

void CFastLED::showColor(const CRGB &color, uint8_t scale)
{
    for (int c = 0; c < count_; c++) {
        controllers_[c].showColor(color, scale);
    }
}

void CFastLED::clearData()
{
    for (int c = 0; c < count_; c++) {
        controllers_[c].clearLedData();
    }
}

void CFastLED::clear(bool writeData)
{
    clearData();
    if (writeData) {
        show(0);
    }
}

void CFastLED::delay(unsigned long ms)
{
    show();
    ::delay(ms);
}

#endif /* BL_PLATFORM_ESP32 || BL_PLATFORM_RP2 */
