/*
 * baseline_teensy.h - Teensy 4.x extras for sketches (BL_TARGET_TEENSY):
 * IntervalTimer, elapsedMillis/elapsedMicros, Teensy's memory-placement
 * macros, digitalWriteFast(). Included by baseline_arduino.h.
 *
 * Interrupt handlers (attachInterrupt, IntervalTimer, attachInterruptVector)
 * run in interrupt context, as on a plain Teensy. imxrt.h, Teensy's register
 * definitions, can be included: Baseline's vector table sits at a fixed address
 * (BL_TEENSY_VECTORS), so its attachInterruptVector() works.
 */
#ifndef BASELINE_TEENSY_H
#define BASELINE_TEENSY_H

#ifdef __cplusplus

/* Code runs from flash and globals sit in DTCM (eDMA reaches it), whatever the
 * sketch asks: these only keep Teensy code compiling. */
#ifndef FASTRUN
#define FASTRUN
#endif
#ifndef FLASHMEM
#define FLASHMEM
#endif
#ifndef DMAMEM
#define DMAMEM
#endif
#ifndef EXTMEM
#define EXTMEM
#endif
#ifndef F_CPU
#define F_CPU 600000000
#endif

/* ---- Teensyduino names for things the ESP32 family names differently ---- */
/* PWM: analogWrite() takes analogWriteResolution() bits (default 8) at the
 * pin's analogWriteFrequency() (Teensy's default per pin, 4.482 kHz on most). */
void analogWriteResolution(unsigned int bits);
void analogWriteFrequency(uint8_t pin, float frequency);
/* Teensy 4.1 PSRAM (EXTMEM chips): like Teensyduino, these fall back to RAM
 * when there is no PSRAM. free() frees either kind. */
static inline void *extmem_malloc(size_t size) { void *p = BL->ps_malloc(size); return p ? p : malloc(size); }
static inline void *extmem_calloc(size_t n, size_t size) { void *p = BL->ps_calloc(n, size); return p ? p : calloc(n, size); }
static inline void *extmem_realloc(void *ptr, size_t size) { void *p = BL->ps_realloc(ptr, size); return p ? p : realloc(ptr, size); }
static inline void extmem_free(void *ptr) { free(ptr); }
/* The die temperature, degrees C. */
static inline float tempmonGetTemp(void) { return BL->temperature_read(); }
#ifdef BL_BOARD_SD_BUILTIN
#define BUILTIN_SDCARD BL_BOARD_SD_BUILTIN      /* SD.begin(BUILTIN_SDCARD): the 4.1's slot */
#endif

/* The same as digitalWrite()/digitalRead() for now (a call into Baseline). */
static inline void digitalWriteFast(uint8_t pin, uint8_t val) { digitalWrite(pin, val); }
static inline uint8_t digitalReadFast(uint8_t pin) { return digitalRead(pin); }
static inline void digitalToggleFast(uint8_t pin) { digitalWrite(pin, !digitalRead(pin)); }

/* ---- more ports: Serial3-Serial7 (Serial8 on the 4.1), Wire2 (sketch_teensy.cpp) ---- */
extern HardwareSerial Serial3;
extern HardwareSerial Serial4;
extern HardwareSerial Serial5;
extern HardwareSerial Serial6;
extern HardwareSerial Serial7;
extern HardwareSerial Serial8;
extern TwoWire Wire2;

/* ---- IntervalTimer: call a function every N microseconds, in interrupt context ---- */
class IntervalTimer {
public:
    constexpr IntervalTimer() {}
    ~IntervalTimer() { end(); }
    bool begin(void (*fn)(), float microseconds) { return start(call_plain, (void *)fn, microseconds); }
    bool begin(void (*fn)(), double microseconds) { return begin(fn, (float)microseconds); }
    bool begin(void (*fn)(), unsigned int microseconds) { return begin(fn, (float)microseconds); }
    bool begin(void (*fn)(), int microseconds) { return begin(fn, (float)microseconds); }
    bool begin(void (*fn)(), unsigned long microseconds) { return begin(fn, (float)microseconds); }
    bool begin(void (*fn)(), long microseconds) { return begin(fn, (float)microseconds); }
    void update(float microseconds) { if (h_ >= 0) bl_isr_timer_update(h_, microseconds); }
    void update(unsigned int microseconds) { update((float)microseconds); }
    void end() { if (h_ >= 0) { bl_isr_timer_end(h_); h_ = -1; } }
    void priority(uint8_t n) { prio_ = n; if (h_ >= 0) bl_isr_timer_priority(h_, n); }
private:
    static void call_plain(void *fn) { ((void (*)(void))fn)(); }
    bool start(bl_callback_t cb, void *arg, float us)
    {
        end();
        h_ = bl_isr_timer_begin(cb, arg, us);
        if (h_ < 0) {
            return false;
        }
        if (prio_ != 128) {
            bl_isr_timer_priority(h_, prio_);
        }
        return true;
    }
    int h_ = -1;
    uint8_t prio_ = 128;
};

/* ---- elapsedMillis / elapsedMicros: variables that count up by themselves ---- */
class elapsedMillis {
public:
    elapsedMillis() : start_(millis()) {}
    elapsedMillis(unsigned long val) : start_(millis() - val) {}
    operator unsigned long() const { return millis() - start_; }
    elapsedMillis &operator=(unsigned long val) { start_ = millis() - val; return *this; }
    elapsedMillis &operator-=(unsigned long val) { start_ += val; return *this; }
    elapsedMillis &operator+=(unsigned long val) { start_ -= val; return *this; }
private:
    unsigned long start_;
};

class elapsedMicros {
public:
    elapsedMicros() : start_(micros()) {}
    elapsedMicros(unsigned long val) : start_(micros() - val) {}
    operator unsigned long() const { return (unsigned long)micros() - start_; }
    elapsedMicros &operator=(unsigned long val) { start_ = micros() - val; return *this; }
    elapsedMicros &operator-=(unsigned long val) { start_ += val; return *this; }
    elapsedMicros &operator+=(unsigned long val) { start_ -= val; return *this; }
private:
    unsigned long start_;
};

#endif /* __cplusplus */
#endif /* BASELINE_TEENSY_H */
