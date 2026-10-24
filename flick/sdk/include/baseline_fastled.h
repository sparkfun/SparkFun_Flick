/*
 * baseline_fastled.h - FastLED-compatible LED strip API for sketches.
 * <FastLED.h> leads here, so ordinary FastLED sketches compile unchanged.
 *
 * Supported: CRGB (with the HTML color names), CHSV, hsv2rgb_rainbow,
 * FastLED.addLeds<CHIPSET, PIN, ORDER>(leds, n) for WS2812/WS2812B/WS2813/
 * WS2811/WS2811_400/SK6812/NEOPIXEL/APA106/PL9823/UCS1903, show(),
 * setBrightness(), clear(), showColor(), delay(), setMaxPowerInVoltsAndMilliamps(),
 * fill_solid, fill_rainbow, fill_gradient_RGB, fadeToBlackBy, nscale8, blur1d,
 * scale8, qadd8, qsub8, sin8, cos8, sin16, cos16, beat8, beatsin8, beatsin16,
 * random8, random16, map8, lerp8by8, EVERY_N_MILLISECONDS, EVERY_N_SECONDS.
 * Not supported: color palettes (CRGBPalette16), SPI chipsets (APA102, WS2801),
 * RGBW, and dithering/temperature correction (accepted, ignored).
 *
 * Baseline drives the strip with the RMT peripheral (any output pin).
 */
#ifndef BASELINE_FASTLED_H
#define BASELINE_FASTLED_H

#ifdef __cplusplus

#define FASTLED_VERSION 3009000     /* the FastLED API this mirrors */

typedef uint8_t fract8;
typedef uint16_t fract16;

/* ---------------- 8-bit math (lib8tion) ---------------- */

static inline uint8_t scale8(uint8_t i, fract8 scale) { return (uint8_t)(((uint16_t)i * (1 + (uint16_t)scale)) >> 8); }
static inline uint8_t scale8_video(uint8_t i, fract8 scale) { return (uint8_t)((((uint16_t)i * scale) >> 8) + ((i && scale) ? 1 : 0)); }
static inline uint16_t scale16(uint16_t i, fract16 scale) { return (uint16_t)(((uint32_t)i * (1 + (uint32_t)scale)) >> 16); }
static inline uint8_t qadd8(uint8_t i, uint8_t j) { unsigned t = i + j; return t > 255 ? 255 : (uint8_t)t; }
static inline uint8_t qsub8(uint8_t i, uint8_t j) { int t = i - j; return t < 0 ? 0 : (uint8_t)t; }
static inline uint8_t qmul8(uint8_t i, uint8_t j) { unsigned p = (unsigned)i * j; return p > 255 ? 255 : (uint8_t)p; }
static inline uint8_t avg8(uint8_t i, uint8_t j) { return (uint8_t)((i + j) >> 1); }
static inline uint8_t map8(uint8_t in, uint8_t rangeStart, uint8_t rangeEnd) { return rangeStart + scale8(in, rangeEnd - rangeStart); }
static inline uint8_t lerp8by8(uint8_t a, uint8_t b, fract8 frac) { return b > a ? a + scale8(b - a, frac) : a - scale8(a - b, frac); }
static inline uint8_t dim8_raw(uint8_t x) { return scale8(x, x); }
static inline uint8_t brighten8_raw(uint8_t x) { uint8_t ix = 255 - x; return 255 - scale8(ix, ix); }

int16_t sin16(uint16_t theta);      /* -32767..32767 over one turn (0..65535) */
static inline int16_t cos16(uint16_t theta) { return sin16(theta + 16384); }
static inline uint8_t sin8(uint8_t theta) { return (uint8_t)((sin16((uint16_t)theta << 8) >> 8) + 128); }
static inline uint8_t cos8(uint8_t theta) { return sin8(theta + 64); }

static inline uint8_t random8() { return (uint8_t)bl_random(); }
static inline uint8_t random8(uint8_t lim) { return (uint8_t)(((uint16_t)random8() * lim) >> 8); }
static inline uint8_t random8(uint8_t min, uint8_t lim) { return min + random8(lim - min); }
static inline uint16_t random16() { return (uint16_t)bl_random(); }
static inline uint16_t random16(uint16_t lim) { return (uint16_t)(((uint32_t)random16() * lim) >> 16); }
static inline uint16_t random16(uint16_t min, uint16_t lim) { return min + random16(lim - min); }
static inline void random16_add_entropy(uint16_t) {}
static inline void random16_set_seed(uint16_t) {}

/* Beats: sawtooth / sine waves at a tempo in beats per minute. */
static inline uint16_t beat88(uint16_t bpm88, uint32_t timebase = 0) { return (uint16_t)(((millis() - timebase) * bpm88 * 280) >> 16); }
static inline uint16_t beat16(uint16_t bpm, uint32_t timebase = 0) { if (bpm < 256) bpm <<= 8; return beat88(bpm, timebase); }
static inline uint8_t beat8(uint16_t bpm, uint32_t timebase = 0) { return (uint8_t)(beat16(bpm, timebase) >> 8); }
static inline uint8_t beatsin8(uint16_t bpm, uint8_t lowest = 0, uint8_t highest = 255, uint32_t timebase = 0, uint8_t phase = 0)
{
    uint8_t s = sin8(beat8(bpm, timebase) + phase);
    return lowest + scale8(s, highest - lowest);
}
static inline uint16_t beatsin16(uint16_t bpm, uint16_t lowest = 0, uint16_t highest = 65535, uint32_t timebase = 0, uint16_t phase = 0)
{
    uint16_t s = (uint16_t)(sin16(beat16(bpm, timebase) + phase) + 32768);
    return lowest + scale16(s, highest - lowest);
}

#define BL_FL_CAT2(a, b) a##b
#define BL_FL_CAT(a, b) BL_FL_CAT2(a, b)
#define EVERY_N_MILLISECONDS(N) \
    static uint32_t BL_FL_CAT(bl_every_, __LINE__) = 0; \
    if ((uint32_t)(millis() - BL_FL_CAT(bl_every_, __LINE__)) >= (uint32_t)(N) && ((BL_FL_CAT(bl_every_, __LINE__) = millis()), true))
#define EVERY_N_SECONDS(N) EVERY_N_MILLISECONDS((N) * 1000UL)
#define EVERY_N_MILLIS(N) EVERY_N_MILLISECONDS(N)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
#endif

/* ---------------- colors ---------------- */

struct CRGB;

struct CHSV {
    union {
        struct {
            union { uint8_t hue; uint8_t h; };
            union { uint8_t saturation; uint8_t sat; uint8_t s; };
            union { uint8_t value; uint8_t val; uint8_t v; };
        };
        uint8_t raw[3];
    };
    CHSV() {}
    constexpr CHSV(uint8_t ih, uint8_t is, uint8_t iv) : hue(ih), sat(is), val(iv) {}
};

void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb);

struct CRGB {
    union {
        struct {
            union { uint8_t r; uint8_t red; };
            union { uint8_t g; uint8_t green; };
            union { uint8_t b; uint8_t blue; };
        };
        uint8_t raw[3];
    };

    typedef enum {
        AliceBlue = 0xF0F8FF, Amethyst = 0x9966CC, AntiqueWhite = 0xFAEBD7, Aqua = 0x00FFFF, Aquamarine = 0x7FFFD4,
        Azure = 0xF0FFFF, Beige = 0xF5F5DC, Bisque = 0xFFE4C4, Black = 0x000000, BlanchedAlmond = 0xFFEBCD,
        Blue = 0x0000FF, BlueViolet = 0x8A2BE2, Brown = 0xA52A2A, BurlyWood = 0xDEB887, CadetBlue = 0x5F9EA0,
        Chartreuse = 0x7FFF00, Chocolate = 0xD2691E, Coral = 0xFF7F50, CornflowerBlue = 0x6495ED, Cornsilk = 0xFFF8DC,
        Crimson = 0xDC143C, Cyan = 0x00FFFF, DarkBlue = 0x00008B, DarkCyan = 0x008B8B, DarkGoldenrod = 0xB8860B,
        DarkGray = 0xA9A9A9, DarkGreen = 0x006400, DarkKhaki = 0xBDB76B, DarkMagenta = 0x8B008B, DarkOliveGreen = 0x556B2F,
        DarkOrange = 0xFF8C00, DarkOrchid = 0x9932CC, DarkRed = 0x8B0000, DarkSalmon = 0xE9967A, DarkSeaGreen = 0x8FBC8F,
        DarkSlateBlue = 0x483D8B, DarkSlateGray = 0x2F4F4F, DarkTurquoise = 0x00CED1, DarkViolet = 0x9400D3,
        DeepPink = 0xFF1493, DeepSkyBlue = 0x00BFFF, DimGray = 0x696969, DodgerBlue = 0x1E90FF, FireBrick = 0xB22222,
        FloralWhite = 0xFFFAF0, ForestGreen = 0x228B22, Fuchsia = 0xFF00FF, Gainsboro = 0xDCDCDC, GhostWhite = 0xF8F8FF,
        Gold = 0xFFD700, Goldenrod = 0xDAA520, Gray = 0x808080, Grey = 0x808080, Green = 0x008000, GreenYellow = 0xADFF2F,
        Honeydew = 0xF0FFF0, HotPink = 0xFF69B4, IndianRed = 0xCD5C5C, Indigo = 0x4B0082, Ivory = 0xFFFFF0,
        Khaki = 0xF0E68C, Lavender = 0xE6E6FA, LavenderBlush = 0xFFF0F5, LawnGreen = 0x7CFC00, LemonChiffon = 0xFFFACD,
        LightBlue = 0xADD8E6, LightCoral = 0xF08080, LightCyan = 0xE0FFFF, LightGreen = 0x90EE90, LightGrey = 0xD3D3D3,
        LightPink = 0xFFB6C1, LightSalmon = 0xFFA07A, LightSeaGreen = 0x20B2AA, LightSkyBlue = 0x87CEFA,
        LightSteelBlue = 0xB0C4DE, LightYellow = 0xFFFFE0, Lime = 0x00FF00, LimeGreen = 0x32CD32, Linen = 0xFAF0E6,
        Magenta = 0xFF00FF, Maroon = 0x800000, MediumBlue = 0x0000CD, MediumOrchid = 0xBA55D3, MediumPurple = 0x9370DB,
        MediumSeaGreen = 0x3CB371, MediumSpringGreen = 0x00FA9A, MediumTurquoise = 0x48D1CC, MidnightBlue = 0x191970,
        MintCream = 0xF5FFFA, MistyRose = 0xFFE4E1, Moccasin = 0xFFE4B5, Navy = 0x000080, OldLace = 0xFDF5E6,
        Olive = 0x808000, OliveDrab = 0x6B8E23, Orange = 0xFFA500, OrangeRed = 0xFF4500, Orchid = 0xDA70D6,
        PaleGreen = 0x98FB98, PaleTurquoise = 0xAFEEEE, PaleVioletRed = 0xDB7093, PeachPuff = 0xFFDAB9, Peru = 0xCD853F,
        Pink = 0xFFC0CB, Plaid = 0xCC5533, Plum = 0xDDA0DD, PowderBlue = 0xB0E0E6, Purple = 0x800080, Red = 0xFF0000,
        RosyBrown = 0xBC8F8F, RoyalBlue = 0x4169E1, SaddleBrown = 0x8B4513, Salmon = 0xFA8072, SandyBrown = 0xF4A460,
        SeaGreen = 0x2E8B57, Seashell = 0xFFF5EE, Sienna = 0xA0522D, Silver = 0xC0C0C0, SkyBlue = 0x87CEEB,
        SlateBlue = 0x6A5ACD, SlateGray = 0x708090, Snow = 0xFFFAFA, SpringGreen = 0x00FF7F, SteelBlue = 0x4682B4,
        Tan = 0xD2B48C, Teal = 0x008080, Thistle = 0xD8BFD8, Tomato = 0xFF6347, Turquoise = 0x40E0D0, Violet = 0xEE82EE,
        Wheat = 0xF5DEB3, White = 0xFFFFFF, WhiteSmoke = 0xF5F5F5, Yellow = 0xFFFF00, YellowGreen = 0x9ACD32,
        FairyLight = 0xFFE42D, FairyLightNCC = 0xFF9D2A,
    } HTMLColorCode;

    CRGB() {}
    constexpr CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
    constexpr CRGB(uint32_t colorcode) : r((colorcode >> 16) & 0xFF), g((colorcode >> 8) & 0xFF), b(colorcode & 0xFF) {}
    constexpr CRGB(HTMLColorCode colorcode) : CRGB((uint32_t)colorcode) {}
    CRGB(const CHSV &hsv) { hsv2rgb_rainbow(hsv, *this); }

    CRGB &operator=(const CHSV &hsv) { hsv2rgb_rainbow(hsv, *this); return *this; }
    CRGB &operator=(uint32_t colorcode) { r = (colorcode >> 16) & 0xFF; g = (colorcode >> 8) & 0xFF; b = colorcode & 0xFF; return *this; }
    uint8_t &operator[](uint8_t x) { return raw[x]; }
    const uint8_t &operator[](uint8_t x) const { return raw[x]; }

    CRGB &setRGB(uint8_t nr, uint8_t ng, uint8_t nb) { r = nr; g = ng; b = nb; return *this; }
    CRGB &setHSV(uint8_t hue, uint8_t sat, uint8_t val) { hsv2rgb_rainbow(CHSV(hue, sat, val), *this); return *this; }
    CRGB &setHue(uint8_t hue) { return setHSV(hue, 255, 255); }
    CRGB &setColorCode(uint32_t colorcode) { return *this = colorcode; }

    CRGB &operator+=(const CRGB &o) { r = qadd8(r, o.r); g = qadd8(g, o.g); b = qadd8(b, o.b); return *this; }
    CRGB &operator-=(const CRGB &o) { r = qsub8(r, o.r); g = qsub8(g, o.g); b = qsub8(b, o.b); return *this; }
    CRGB &operator+=(uint8_t d) { r = qadd8(r, d); g = qadd8(g, d); b = qadd8(b, d); return *this; }
    CRGB &operator-=(uint8_t d) { r = qsub8(r, d); g = qsub8(g, d); b = qsub8(b, d); return *this; }
    CRGB &operator*=(uint8_t d) { r = qmul8(r, d); g = qmul8(g, d); b = qmul8(b, d); return *this; }
    CRGB &operator/=(uint8_t d) { r /= d; g /= d; b /= d; return *this; }
    CRGB &operator%=(uint8_t scale) { return nscale8_video(scale); }
    CRGB &operator|=(const CRGB &o) { if (o.r > r) r = o.r; if (o.g > g) g = o.g; if (o.b > b) b = o.b; return *this; }
    CRGB &operator&=(const CRGB &o) { if (o.r < r) r = o.r; if (o.g < g) g = o.g; if (o.b < b) b = o.b; return *this; }
    CRGB &operator++() { return *this += 1; }
    CRGB &operator--() { return *this -= 1; }

    CRGB &nscale8(uint8_t scale) { r = ::scale8(r, scale); g = ::scale8(g, scale); b = ::scale8(b, scale); return *this; }
    CRGB &nscale8(const CRGB &s) { r = ::scale8(r, s.r); g = ::scale8(g, s.g); b = ::scale8(b, s.b); return *this; }
    CRGB &nscale8_video(uint8_t scale) { r = ::scale8_video(r, scale); g = ::scale8_video(g, scale); b = ::scale8_video(b, scale); return *this; }
    CRGB &fadeToBlackBy(uint8_t fade) { return nscale8(255 - fade); }
    CRGB &fadeLightBy(uint8_t fade) { return nscale8_video(255 - fade); }
    uint8_t getLuma() const { return ::scale8(r, 54) + ::scale8(g, 183) + ::scale8(b, 18); }
    uint8_t getAverageLight() const { return (uint8_t)((r + g + b) / 3); }
    void maximizeBrightness(uint8_t limit = 255)
    {
        uint8_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m) { r = (uint16_t)r * limit / m; g = (uint16_t)g * limit / m; b = (uint16_t)b * limit / m; }
    }
    explicit operator bool() const { return r || g || b; }
    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB &o) const { return !(*this == o); }
};

static inline CRGB operator+(const CRGB &a, const CRGB &b) { CRGB c(a); return c += b; }
static inline CRGB operator-(const CRGB &a, const CRGB &b) { CRGB c(a); return c -= b; }
static inline CRGB operator*(const CRGB &a, uint8_t d) { CRGB c(a); return c *= d; }
static inline CRGB operator%(const CRGB &a, uint8_t d) { CRGB c(a); return c %= d; }
static inline CRGB blend(const CRGB &a, const CRGB &b, fract8 amountOfB)
{
    return CRGB(lerp8by8(a.r, b.r, amountOfB), lerp8by8(a.g, b.g, amountOfB), lerp8by8(a.b, b.b, amountOfB));
}

/* Fill / fade helpers */
void fill_solid(CRGB *leds, int numToFill, const CRGB &color);
void fill_rainbow(CRGB *leds, int numToFill, uint8_t initialhue, uint8_t deltahue = 5);
void fill_rainbow_circular(CRGB *leds, int numToFill, uint8_t initialhue, bool reversed = false);
void fill_gradient_RGB(CRGB *leds, uint16_t numLeds, const CRGB &c1, const CRGB &c2);
void fadeToBlackBy(CRGB *leds, uint16_t numLeds, uint8_t fadeBy);
void fade_raw(CRGB *leds, uint16_t numLeds, uint8_t fadeBy);
void nscale8(CRGB *leds, uint16_t numLeds, uint8_t scale);
void nscale8_video(CRGB *leds, uint16_t numLeds, uint8_t scale);
void blur1d(CRGB *leds, uint16_t numLeds, fract8 blur_amount);

/* ---------------- chipsets and color orders ---------------- */

enum EOrder { RGB = 0012, RBG = 0021, GRB = 0102, GBR = 0120, BRG = 0201, BGR = 0210 };

#define BL_FL_CHIPSET(NAME, TIMING) \
    template <uint8_t DATA_PIN, EOrder RGB_ORDER = RGB> struct NAME { enum { chipset = TIMING }; };
BL_FL_CHIPSET(WS2812, BL_STRIP_WS2812)
BL_FL_CHIPSET(WS2812B, BL_STRIP_WS2812)
BL_FL_CHIPSET(WS2813, BL_STRIP_WS2812)
BL_FL_CHIPSET(WS2815, BL_STRIP_WS2812)
BL_FL_CHIPSET(WS2811, BL_STRIP_WS2812)
BL_FL_CHIPSET(WS2811_400, BL_STRIP_WS2811)
BL_FL_CHIPSET(SK6812, BL_STRIP_SK6812)
BL_FL_CHIPSET(SK6822, BL_STRIP_SK6812)
BL_FL_CHIPSET(APA106, BL_STRIP_WS2812)
BL_FL_CHIPSET(PL9823, BL_STRIP_WS2812)
BL_FL_CHIPSET(UCS1903, BL_STRIP_WS2811)
BL_FL_CHIPSET(TM1809, BL_STRIP_WS2812)
BL_FL_CHIPSET(GS1903, BL_STRIP_WS2812)
/* NEOPIXEL takes only the pin; its colors are GRB. */
template <uint8_t DATA_PIN> struct NEOPIXEL { enum { chipset = BL_STRIP_WS2812, order = GRB }; };

/* Ignored by Baseline (accepted for compatibility). */
enum LEDColorCorrection { TypicalSMD5050 = 0xFFB0F0, TypicalLEDStrip = 0xFFB0F0, Typical8mmPixel = 0xFFE08C, TypicalPixelString = 0xFFE08C, UncorrectedColor = 0xFFFFFF };
enum ColorTemperature { Candle = 0xFF9329, Tungsten40W = 0xFFC58F, Tungsten100W = 0xFFD6AA, Halogen = 0xFFF1E0, CarbonArc = 0xFFFAF4, HighNoonSun = 0xFFFFFB, DirectSunlight = 0xFFFFFF, OvercastSky = 0xC9E2FF, ClearBlueSky = 0x409CFF, UncorrectedTemperature = 0xFFFFFF };
#define DISABLE_DITHER 0x00
#define BINARY_DITHER  0x01

class CLEDController {
public:
    CLEDController &setCorrection(CRGB) { return *this; }
    CLEDController &setCorrection(LEDColorCorrection) { return *this; }
    CLEDController &setTemperature(CRGB) { return *this; }
    CLEDController &setTemperature(ColorTemperature) { return *this; }
    CLEDController &setDither(uint8_t = BINARY_DITHER) { return *this; }
    CLEDController &setLeds(CRGB *data, int n) { leds_ = data; count_ = n; return *this; }
    int size() const { return count_; }
    CRGB *leds() { return leds_; }
    CRGB &operator[](int x) { return leds_[x]; }
    void showLeds(uint8_t brightness);
    void showColor(const CRGB &c, uint8_t brightness);
    void clearLedData() { if (leds_) memset((void *)leds_, 0, sizeof(CRGB) * count_); }

    CRGB *leds_ = 0;
    int count_ = 0;
    int8_t pin_ = -1;
    uint8_t chipset_ = 0;
    uint16_t order_ = RGB;
};

class CFastLED {
public:
    static const int MAX_CONTROLLERS = 8;

    template <template <uint8_t DATA_PIN, EOrder RGB_ORDER> class CHIPSET, uint8_t DATA_PIN, EOrder RGB_ORDER = RGB>
    CLEDController &addLeds(CRGB *data, int nLedsOrOffset, int nLedsIfOffset = 0)
    {
        return add(DATA_PIN, CHIPSET<DATA_PIN, RGB_ORDER>::chipset, RGB_ORDER, data, nLedsOrOffset, nLedsIfOffset);
    }
    template <template <uint8_t DATA_PIN> class CHIPSET, uint8_t DATA_PIN>
    CLEDController &addLeds(CRGB *data, int nLedsOrOffset, int nLedsIfOffset = 0)
    {
        return add(DATA_PIN, CHIPSET<DATA_PIN>::chipset, (EOrder)CHIPSET<DATA_PIN>::order, data, nLedsOrOffset, nLedsIfOffset);
    }

    void setBrightness(uint8_t scale) { brightness_ = scale; }
    uint8_t getBrightness() { return brightness_; }
    void setMaxPowerInVoltsAndMilliamps(uint8_t volts, uint32_t milliamps) { maxMilliwatts_ = (uint32_t)volts * milliamps; }
    void setMaxPowerInMilliWatts(uint32_t milliwatts) { maxMilliwatts_ = milliwatts; }
    void setMaxRefreshRate(uint16_t, bool = false) {}
    void setCorrection(CRGB) {}
    void setCorrection(LEDColorCorrection) {}
    void setTemperature(CRGB) {}
    void setTemperature(ColorTemperature) {}
    void setDither(uint8_t = BINARY_DITHER) {}
    void show() { show(brightness_); }
    void show(uint8_t scale);
    void showColor(const CRGB &color) { showColor(color, brightness_); }
    void showColor(const CRGB &color, uint8_t scale);
    void clear(bool writeData = false);
    void clearData();
    void delay(unsigned long ms);
    int count() { return count_; }
    CLEDController &operator[](int x) { return controllers_[x < count_ ? x : 0]; }
    int size() { return count_ ? controllers_[0].size() : 0; }
    CRGB *leds() { return count_ ? controllers_[0].leds() : 0; }
    uint16_t getFPS() { return 0; }
    void countFPS(int = 25) {}

private:
    CLEDController &add(int pin, int chipset, EOrder order, CRGB *data, int nLedsOrOffset, int nLedsIfOffset);
    uint8_t powerLimitedBrightness(uint8_t scale);
    CLEDController controllers_[MAX_CONTROLLERS];
    int count_ = 0;
    uint8_t brightness_ = 255;
    uint32_t maxMilliwatts_ = 0;
};

extern CFastLED FastLED;

#endif /* __cplusplus */
#endif /* BASELINE_FASTLED_H */
